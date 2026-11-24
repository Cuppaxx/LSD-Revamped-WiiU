/* menu.c - see menu.h. Art: ETC/TITLE.TIM (the original title sprite sheet: glowing rings with the faces and the LSD logo on a
 * navy background) and ETC/HGRAPH.TIM (the original dream chart). Sounds: MENU/hover.wav, MENU/press.wav (Revamped's
 * UIButtonSounds), music MENU/<normal|kanji|downer|upper>.wav picked by the day number like Revamped's UIMainMenuMusic. */
#include "menu.h"
#include "save.h"
#include "audio.h"
#include "lsd.h"
#include "sfx.h"
#include "input.h"
#include "flashback.h"
#include "view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- font: 5x7, ASCII 32..95, one byte per row, bit 4 = left */
const uint8_t font57[64][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*   */
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04},  /* ! */
  {0x0a,0x0a,0x00,0x00,0x00,0x00,0x00},  /* " */
  {0x0a,0x0a,0x1f,0x0a,0x1f,0x0a,0x0a},  /* # */
  {0x04,0x0f,0x14,0x0e,0x05,0x1e,0x04},  /* $ */
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03},  /* % */
  {0x0c,0x12,0x14,0x08,0x15,0x12,0x0d},  /* & */
  {0x04,0x04,0x08,0x00,0x00,0x00,0x00},  /* ' */
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02},  /* ( */
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08},  /* ) */
  {0x00,0x04,0x15,0x0e,0x15,0x04,0x00},
  {0x00,0x04,0x04,0x1f,0x04,0x04,0x00},  /* + */
  {0x00,0x00,0x00,0x00,0x0c,0x04,0x08},  /* , */
  {0x00,0x00,0x00,0x1f,0x00,0x00,0x00},  /* - */
  {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c},  /* . */
  {0x00,0x01,0x02,0x04,0x08,0x10,0x00},
  {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},  /* 0 */
  {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},  /* 1 */
  {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},  /* 2 */
  {0x1f,0x02,0x04,0x02,0x01,0x11,0x0e},  /* 3 */
  {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},  /* 4 */
  {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e},  /* 5 */
  {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e},  /* 6 */
  {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},  /* 7 */
  {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},  /* 8 */
  {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c},  /* 9 */
  {0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00},  /* : */
  {0x00,0x0c,0x0c,0x00,0x0c,0x04,0x08},  /* ; */
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02},  /* < */
  {0x00,0x00,0x1f,0x00,0x1f,0x00,0x00},  /* = */
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08},  /* > */
  {0x0e,0x11,0x01,0x02,0x04,0x00,0x04},  /* ? */
  {0x0e,0x11,0x01,0x0d,0x15,0x15,0x0e},  /* @ */
  {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},  /* A */
  {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},  /* B */
  {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},  /* C */
  {0x1c,0x12,0x11,0x11,0x11,0x12,0x1c},  /* D */
  {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},  /* E */
  {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},  /* F */
  {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},  /* G */
  {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},  /* H */
  {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},  /* I */
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0c},  /* J */
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11},  /* K */
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},  /* L */
  {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},  /* M */
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11},  /* N */
  {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},  /* O */
  {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},  /* P */
  {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},  /* Q */
  {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},  /* R */
  {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},  /* S */
  {0x1f,0x04,0x04,0x04,0x04,0x04,0x04},  /* T */
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},  /* U */
  {0x11,0x11,0x11,0x11,0x11,0x0a,0x04},  /* V */
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},  /* W */
  {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},  /* X */
  {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},  /* Y */
  {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f},  /* Z */
  {0x0e,0x08,0x08,0x08,0x08,0x08,0x0e},  /* [ */
  {0x00,0x10,0x08,0x04,0x02,0x01,0x00},
  {0x0e,0x02,0x02,0x02,0x02,0x02,0x0e},  /* ] */
  {0x04,0x0a,0x11,0x00,0x00,0x00,0x00},  /* ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1f},  /* _ */
};

static inline void put_px(Frame *f, int x, int y, uint32_t c) { if ((unsigned)x < (unsigned)f->w && (unsigned)y < (unsigned)f->h) f->color[y * f->w + x] = c; }
static void fill(Frame *f, int x, int y, int w, int h, uint32_t c) {
  for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) put_px(f, xx, yy, c);
}
static void glyphs(Frame *f, int x, int y, const char *s, uint32_t c, int sc) {
  for (; *s; s++, x += 6 * sc) {
    int ch = (unsigned char)*s; if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) continue;
    const uint8_t *g = font57[ch - 32];
    for (int r = 0; r < 7; r++) for (int k = 0; k < 5; k++) if (g[r] & (16 >> k)) fill(f, x + k * sc, y + r * sc, sc, sc, c);
  }
}
void ui_text(Frame *f, int x, int y, const char *s, uint32_t c, int sc) {
  if (sc < 1) sc = 1;
  glyphs(f, x + sc, y + sc, s, 0xFF080418u, sc);        /* drop shadow */
  glyphs(f, x, y, s, c, sc);
}
int ui_text_w(const char *s, int sc) { int n = (int)strlen(s); return n ? n * 6 * sc - sc : 0; }
static void text_c(Frame *f, int cx, int y, const char *s, uint32_t c, int sc) { ui_text(f, cx - ui_text_w(s, sc) / 2, y, s, c, sc); }

void ui_rect(Frame *f, int x, int y, int w, int h, uint32_t c, int a) {
  int R = c >> 16 & 255, G = c >> 8 & 255, B = c & 255;
  for (int yy = y; yy < y + h; yy++) {
    if ((unsigned)yy >= (unsigned)f->h) continue;
    for (int xx = x; xx < x + w; xx++) {
      if ((unsigned)xx >= (unsigned)f->w) continue;
      uint32_t d = f->color[yy * f->w + xx];
      int r = ((d >> 16 & 255) * (256 - a) + R * a) >> 8, g = ((d >> 8 & 255) * (256 - a) + G * a) >> 8, b = ((d & 255) * (256 - a) + B * a) >> 8;
      f->color[yy * f->w + xx] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
  }
}

/* ---------------------------------------------------------------- art */
uint32_t *title_px; int title_w, title_h; static uint32_t *chart_px; static int chart_w, chart_h;
#define KEY_R 24
#define KEY_G 8
#define KEY_B 65            /* TITLE.TIM background navy */
#define CELL 74             /* TITLE.TIM ring cells: 4x3 grid of 74x74 starting at (12,8) */

/* add the art's difference from its navy background onto the frame, so the glow blends with an animated background */
static void blit_delta(Frame *f, int sx, int sy, int w, int h, int dx, int dy, int a) {
  if (!title_px) return;
  for (int y = 0; y < h; y++) {
    int fy = dy + y, ty = sy + y; if ((unsigned)fy >= (unsigned)f->h || (unsigned)ty >= (unsigned)title_h) continue;
    for (int x = 0; x < w; x++) {
      int fx = dx + x, tx = sx + x; if ((unsigned)fx >= (unsigned)f->w || (unsigned)tx >= (unsigned)title_w) continue;
      uint32_t s = title_px[ty * title_w + tx], d = f->color[fy * f->w + fx];
      int r = (int)(d >> 16 & 255) + (((int)(s >> 16 & 255) - KEY_R) * a >> 8);
      int g = (int)(d >> 8 & 255) + (((int)(s >> 8 & 255) - KEY_G) * a >> 8);
      int b = (int)(d & 255) + (((int)(s & 255) - KEY_B) * a >> 8);
      r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
      f->color[fy * f->w + fx] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
  }
}
static void ring(Frame *f, int col, int row, int cx, int cy, int a) { blit_delta(f, 12 + col * CELL, 8 + row * CELL, CELL, CELL, cx - CELL / 2, cy - CELL / 2, a); }
static void face_icon(Frame *f, int x, int y) { blit_delta(f, 267, 157, 10, 10, x, y, 256); }   /* the small yellow face */

static uint8_t *snd_hover, *snd_press;
void menu_init(void) {
  size_t n; uint8_t *b;
  if ((b = lsd_read_file("ETC/TITLE.TIM", &n))) { title_px = lsd_decode_tim(b, n, &title_w, &title_h); free(b); }
  if ((b = lsd_read_file("ETC/HGRAPH.TIM", &n))) { chart_px = lsd_decode_tim(b, n, &chart_w, &chart_h); free(b); }
  if ((b = lsd_read_file("MENU/hover.wav", &n))) { audio_sfx_load(SFX_HOVER, b, n); free(b); snd_hover = (uint8_t *)1; }
  if ((b = lsd_read_file("MENU/press.wav", &n))) { audio_sfx_load(SFX_PRESS, b, n); free(b); snd_press = (uint8_t *)1; }
  lsd_log("menu art: title %s (%dx%d), chart %s, sounds %s", title_px ? "ok" : "MISSING", title_w, title_h, chart_px ? "ok" : "MISSING",
          snd_hover && snd_press ? "ok" : "missing");
}

const char *menu_music_for_day(int day) {
  int m = day % 41;
  return m <= 10 ? "MENU/normal.wav" : m <= 20 ? "MENU/kanji.wav" : m <= 30 ? "MENU/downer.wav" : "MENU/upper.wav";
}


/* ---------------------------------------------------------------- screens and items
 * Title (S_MAIN) and pause: a centred list whose highlighted item grows to 2x. OPTIONS is a hub of categories
 * (GAMEPLAY, CONTROLS, AUDIO, GRAPHICS, DREAMS, CREDITS); each category is a page of grouped settings (I_SEP rows are
 * section headings) with a value pill or a slider, and a one-line description of the highlighted setting in the footer.
 * DREAM SELECT shows the original and the additional dreams as two columns. Every screen is laid out from the frame's
 * width (320 for 4:3, 426 for 16:9; always 240 lines). */
enum { S_MAIN, S_PAUSE, S_DREAMS, S_OPTIONS, S_GRAPHICS, S_CHART, S_CONFIRM_NEW, S_CONTROLS, S_GAMEPLAY, S_AUDIO, S_DREAMOPT, S_COUNT };
enum {   /* item ids */
  I_CONTINUE, I_NEW, I_DREAMS, I_CHART, I_OPTIONS, I_QUIT, I_RESUME, I_TITLE, I_SAVEQUIT,
  I_SENS, I_INVERT, I_BOB, I_RUN, I_MUSIC, I_SFX, I_GFX, I_BACK,
  I_DIST, I_RES, I_ASPECT, I_SCALE, I_FOG, I_SKY, I_SNAP, I_MODELS, I_FPS, I_THREADS,
  I_YES, I_NO, I_ADDL, I_SEP, I_AFFINE, I_FOOT, I_CONTROLS, I_SCHEME, I_DEADZONE, I_RESETBIND,
  I_FLASHBACK, I_JOURNAL, I_SPDAYS, I_INTRO, I_CREDITS,   /* flashback.c, journal.c, special.c */
  I_CAT_GAME, I_CAT_AUDIO, I_CAT_DREAMS,                  /* OPTIONS hub (with I_CONTROLS, I_GFX, I_CREDITS) */
  I_VCAP, I_VTARGET,                                      /* AUTO view distance: limit, frame rate */
  I_BIND0 = 60, I_DREAM0 = 100
};
typedef struct { int id; char label[40]; char value[24]; int slider, smax; /* slider: -1 none, else 0..smax */ const char *hint; char sub[32]; } Item;
#define MAX_ITEMS MENU_MAXI

static const int res_h[] = { 240, 300, 360, 480 };
static char s_hint_buf[64], s_hint_view[64];
static void view_label(char *out, size_t n, int c) {
  if (c == VIEW_AUTO) snprintf(out, n, "AUTO"); else if (c == VIEW_CLASSIC) snprintf(out, n, "CLASSIC");
  else if (c == VIEW_MAX) snprintf(out, n, "MAX"); else snprintf(out, n, "%d", view_tiles(c));
}

int menu_frame_w(int aspect) { return aspect ? 426 : 320; }

static int build(const Menu *m, const MenuCtx *c, Item *it) {
  int n = 0;
#define ADD(ID, LABEL) do { memset(&it[n], 0, sizeof it[n]); it[n].id = (ID); snprintf(it[n].label, sizeof it[n].label, "%s", (LABEL)); it[n].slider = -1; n++; } while (0)
#define VAL(...) snprintf(it[n - 1].value, sizeof it[n - 1].value, __VA_ARGS__)
#define SUB(...) snprintf(it[n - 1].sub, sizeof it[n - 1].sub, __VA_ARGS__)
#define ONOFF(v) VAL("%s", (v) ? "ON" : "OFF")
#define HINT(h) (it[n - 1].hint = (h))
#define SLIDE(v, mx) (it[n - 1].slider = (v), it[n - 1].smax = (mx))
  const Options *o = &g_opt;
  int has_addl = c->dl && c->dl->n > c->dl->norig;
  switch (m->screen) {
    case S_MAIN:
      if (g_save.has_game) { ADD(I_CONTINUE, "CONTINUE"); snprintf(s_hint_buf, sizeof s_hint_buf, "DAY %d OF YOUR DREAM JOURNAL", g_save.day); HINT(s_hint_buf); }
      ADD(I_NEW, "NEW GAME"); HINT("A NEW DREAM JOURNAL, FROM DAY 1");
      if (g_save.has_game && fb_can()) { ADD(I_FLASHBACK, "FLASHBACK"); HINT("RELIVE MOMENTS OF EARLIER DAYS"); }   /* DreamSystem.CanFlashback */
      ADD(I_DREAMS, "DREAM SELECT"); HINT("START A DAY IN ANY DREAM");
      ADD(I_CHART, "DREAM CHART"); HINT("WHERE YOUR DAYS HAVE TAKEN YOU");
      if (g_save.has_game) { ADD(I_JOURNAL, "JOURNAL"); HINT("YOUR DAYS, DREAMS AND SPECIAL DAYS"); }
      ADD(I_OPTIONS, "OPTIONS"); HINT("GAMEPLAY, CONTROLS, AUDIO, GRAPHICS");
      ADD(I_QUIT, "QUIT"); HINT("BACK TO THE WII U MENU");
      break;
    case S_PAUSE:
      ADD(I_RESUME, "RESUME"); HINT("BACK TO THE DREAM");
      ADD(I_DREAMS, "DREAM SELECT"); HINT("GO TO ANOTHER DREAM NOW (SAME DAY)");
      ADD(I_CHART, "DREAM CHART"); HINT("WHERE YOUR DAYS HAVE TAKEN YOU");
      ADD(I_JOURNAL, "JOURNAL"); HINT("YOUR DAYS, DREAMS AND SPECIAL DAYS");
      ADD(I_OPTIONS, "OPTIONS"); HINT("GAMEPLAY, CONTROLS, AUDIO, GRAPHICS");
      ADD(I_TITLE, "RETURN TO TITLE"); HINT("GIVE UP THIS DAY (THE DAY IS NOT COUNTED)");
      ADD(I_SAVEQUIT, "SAVE & QUIT"); HINT("SAVE AND LEAVE TO THE WII U MENU");
      break;
    case S_DREAMS:                                  /* the originals, then (option on) the Additional Dreams: two columns */
      for (int i = 0; c->dl && i < c->dl->n && n < MAX_ITEMS; i++) {
        if (i >= c->dl->norig && !g_opt.addl) break;
        ADD(I_DREAM0 + i, c->dl->d[i].name);
      }
      break;
    case S_OPTIONS: {                               /* the hub */
      int h = res_h[o->res], w = o->aspect ? (h * 16 / 9) & ~1 : h * 4 / 3;
      ADD(I_CAT_GAME, "GAMEPLAY"); SUB("LOOK %d, RUN %s", o->look_sens, o->run_mode ? "TOGGLE" : "HOLD"); HINT("LOOK SPEED, INVERT LOOK, HEAD BOB, RUN MODE");
      ADD(I_CONTROLS, "CONTROLS"); SUB("%s SCHEME", o->scheme ? "TANK" : "FPS"); HINT("CONTROL SCHEME, STICK DEADZONE, BUTTONS");
      ADD(I_CAT_AUDIO, "AUDIO"); SUB("MUSIC %d  SOUND %d", o->music_vol, o->sfx_vol); HINT("MUSIC AND SOUND VOLUME, FOOTSTEPS");
      ADD(I_GFX, "GRAPHICS"); SUB("%dX%d %s", w, h, o->aspect ? "16:9" : "4:3"); HINT("RESOLUTION, DISTANCE, PS1 LOOK, THREADS");
      ADD(I_CAT_DREAMS, "DREAMS"); SUB("SPECIAL DAYS %s", o->specialdays ? "ON" : "OFF");
      HINT(has_addl ? "ADDITIONAL DREAMS, SPECIAL DAYS, INTRO MOVIE" : "SPECIAL DAYS, INTRO MOVIE");
      ADD(I_CREDITS, "CREDITS"); HINT("WHO MADE ALL THIS");
      break; }
    case S_GAMEPLAY:
      ADD(I_SEP, "CAMERA");
      ADD(I_SENS, "LOOK SPEED"); SLIDE(o->look_sens, 10); VAL("%d", o->look_sens); HINT("HOW FAST THE STICK TURNS THE VIEW");
      ADD(I_INVERT, "INVERT LOOK"); ONOFF(o->invert_y); HINT("PUSH UP TO LOOK DOWN");
      ADD(I_BOB, "HEAD BOB"); ONOFF(o->head_bob); HINT("THE VIEW SWAYS AS YOU WALK");
      ADD(I_SEP, "MOVEMENT");
      ADD(I_RUN, "RUN MODE"); VAL("%s", o->run_mode ? "TOGGLE" : "HOLD"); HINT("HOLD THE RUN BUTTON, OR TAP IT TO TOGGLE");
      break;
    case S_AUDIO:
      ADD(I_SEP, "VOLUME");
      ADD(I_MUSIC, "MUSIC VOLUME"); SLIDE(o->music_vol, 10); VAL("%d", o->music_vol); HINT("THE DREAM'S SONG AND THE TITLE MUSIC");
      ADD(I_SFX, "SOUND VOLUME"); SLIDE(o->sfx_vol, 10); VAL("%d", o->sfx_vol); HINT("EVERYTHING ELSE (PLAYS THE SOUND TEST)");
      ADD(I_SEP, "EFFECTS");
      ADD(I_FOOT, "FOOTSTEPS"); ONOFF(o->footsteps); HINT("A STEP SOUND THAT FITS THE FLOOR");
      break;
    case S_DREAMOPT:
      ADD(I_SEP, "DREAMS");
      if (has_addl) { ADD(I_ADDL, "ADDITIONAL DREAMS"); ONOFF(o->addl); HINT("14 MORE DREAMS (FIGGLEWATTS' MOD) MIXED IN"); }
      ADD(I_SPDAYS, "SPECIAL DAYS"); ONOFF(o->specialdays); HINT("FILMS AND TEXTS ON SOME DAYS");
      ADD(I_SEP, "START");
      ADD(I_INTRO, "INTRO MOVIE"); ONOFF(o->intro); HINT("AN OPENING FILM AT BOOT (FROM THE SD CARD)");
      break;
    case S_GRAPHICS: {
      int h = res_h[o->res], w = o->aspect ? (h * 16 / 9) & ~1 : h * 4 / 3;
      ADD(I_SEP, "DISPLAY");
      ADD(I_RES, "RESOLUTION"); VAL("%dX%d", w, h); HINT("HIGHER = SLOWER (320X240 = CLASSIC)");
      ADD(I_ASPECT, "ASPECT"); VAL("%s", o->aspect ? "16:9" : "4:3"); HINT("16:9 SHOWS MORE TO THE SIDES (SLOWER)");
      ADD(I_SCALE, "SCALING"); VAL("%s", o->scaling == 0 ? "FIT" : o->scaling == 1 ? "INTEGER" : "STRETCH"); HINT("INTEGER = SHARP PIXELS, MAY BE SMALLER");
      ADD(I_SEP, "WORLD");
      { char lb[16]; view_label(lb, sizeof lb, o->view);
        ADD(I_DIST, "RENDER DISTANCE"); SLIDE(o->view, VIEW_N - 1); VAL("%s", lb);
        if (o->view == VIEW_AUTO) { if (g_view_now > 0) snprintf(s_hint_view, sizeof s_hint_view, "AS FAR AS %d FPS ALLOWS (NOW %d TILES)", o->vtarget ? 30 : 60, g_view_now);
                                    else snprintf(s_hint_view, sizeof s_hint_view, "AS FAR AS %d FPS ALLOWS, ADAPTS AS YOU PLAY", o->vtarget ? 30 : 60); }
        else if (o->view == VIEW_CLASSIC) snprintf(s_hint_view, sizeof s_hint_view, "20 TILES, LIKE THE ORIGINAL");
        else if (o->view == VIEW_MAX) snprintf(s_hint_view, sizeof s_hint_view, "%d TILES: ALL OF IT, MAY DROP FRAMES", view_tiles(VIEW_MAX));
        else snprintf(s_hint_view, sizeof s_hint_view, "%d TILES. FURTHER = SLOWER", view_tiles(o->view));
        HINT(s_hint_view); }
      if (o->view == VIEW_AUTO) {
        char lb[16]; view_label(lb, sizeof lb, o->vcap);
        ADD(I_VCAP, "AUTO LIMIT"); SLIDE(o->vcap - VIEW_CAP_MIN, VIEW_MAX - VIEW_CAP_MIN); VAL("%s", lb); HINT("THE FURTHEST AUTO MAY SEE (TILES)");
        ADD(I_VTARGET, "AUTO TARGET"); VAL("%s", o->vtarget ? "30 FPS" : "60 FPS"); HINT("60 = SMOOTH, 30 = FURTHER BUT CHOPPIER");
      }
      ADD(I_FOG, "FOG"); VAL("%s", o->fogmode == FOG_CLASSIC ? "CLASSIC" : o->fogmode == FOG_SCALED ? "SCALED" : "OFF");
      HINT(o->fogmode == FOG_CLASSIC ? "THE DREAM'S OWN FOG; THE VIEW ENDS IN IT" : o->fogmode == FOG_SCALED ? "STRETCHED OVER THE VIEW, FADES INTO THE SKY" : "NO FOG: SHARP UP TO THE EDGE OF THE VIEW");
      ADD(I_SKY, "SKY EFFECTS"); ONOFF(o->skyfx); HINT("SUNS, CLOUDS AND STARS");
      ADD(I_MODELS, "OBJECT MODELS"); ONOFF(o->models); HINT("ANIMATED OBJECTS (SCRIPTS STILL RUN)");
      ADD(I_SEP, "PS1 LOOK");
      ADD(I_SNAP, "VERTEX SNAPPING"); ONOFF(o->snap); HINT("THE PS1 WOBBLE");
      ADD(I_AFFINE, "TEXTURE WARPING"); SLIDE(o->affine, 4); VAL("%d.%02d", o->affine / 4, (o->affine % 4) * 25); HINT("0 = CORRECT, 1 = PS1 AFFINE (0.5 = REVAMPED)");
      ADD(I_SEP, "PERFORMANCE");
      ADD(I_THREADS, "RENDER THREADS"); SLIDE(o->threads - 1, 3); VAL("%d", o->threads); HINT("THE WII U HAS 3 CORES");
      ADD(I_FPS, "SHOW FPS"); ONOFF(o->show_fps); HINT("FRAME RATE AND VIEW DISTANCE IN THE CORNER");
      break; }
    case S_CONFIRM_NEW:
      ADD(I_NO, "NO"); HINT("KEEP YOUR JOURNAL");
      ADD(I_YES, "YES, START OVER"); HINT("THE JOURNAL IS GONE FOR GOOD");
      break;
    case S_CONTROLS:
      ADD(I_SEP, "STICKS");
      ADD(I_SCHEME, "CONTROL SCHEME"); VAL("%s", o->scheme ? "TANK" : "FPS");
      HINT(o->scheme ? "CLASSIC: STEPS, STICK TURNS, LOOK BUTTONS" : "REVAMPED: STICK MOVES, RIGHT STICK LOOKS");
      ADD(I_DEADZONE, "STICK DEADZONE"); SLIDE(o->deadzone, 8); VAL("%d%%", o->deadzone * 5); HINT("RAISE IF THE VIEW DRIFTS ON ITS OWN");
      ADD(I_SEP, "BUTTONS");
      for (int k = 0; k < ACT_N; k++) {
        ADD(I_BIND0 + k, input_action_name(k)); VAL("%s", input_button_name(o->bind[k]));
        HINT(k == ACT_LOOKUP || k == ACT_LOOKDOWN ? "TANK SCHEME (FPS: RIGHT STICK). A: REBIND" : k == ACT_PAUSE ? "+ ALWAYS PAUSES TOO. A: REBIND" :
             k == ACT_LOOKBEHIND ? "HOLD TO TURN THE VIEW ROUND. A: REBIND" : "A: PRESS A NEW BUTTON FOR THIS");
      }
      ADD(I_RESETBIND, "RESET BUTTONS"); HINT("BACK TO THE DEFAULT BUTTONS");
      break;
  }
  return n;
#undef ADD
#undef VAL
#undef SUB
#undef ONOFF
#undef HINT
#undef SLIDE
}

/* the first row of the additional dreams in the dream select list (= n when there are none) */
static int dream_split(const MenuCtx *c, const Item *it, int n) {
  for (int i = 0; i < n; i++) if (c->dl && it[i].id - I_DREAM0 >= c->dl->norig) return i;
  return n;
}
static void fix_sel(const Item *it, int n, int *sel) {          /* headings are not selectable */
  if (*sel >= n) *sel = n - 1;
  if (*sel < 0) *sel = 0;
  for (int k = 0; k < n && it[*sel].id == I_SEP; k++) *sel = (*sel + 1) % n;
}

static void push(Menu *m, int s) { if (m->depth < 8) m->stack[m->depth++] = m->screen; m->screen = s; m->sel[s] = 0; m->tr = 0; m->tr_dir = 1; m->snap = 1; }
static int pop(Menu *m) { if (!m->depth) return 0; int from = m->screen; m->screen = m->stack[--m->depth]; m->tr = 0; m->tr_dir = -1; m->snap = 1; return from; }

void menu_open(Menu *m, int pause) {
  memset(m, 0, sizeof *m);
  m->screen = pause ? S_PAUSE : S_MAIN; m->tr_dir = 1; m->snap = 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* change an option by d (-1/+1); returns 1 if something changed */
static int change(int id, int d) {
  Options *o = &g_opt; int old;
  switch (id) {
    case I_SENS: old = o->look_sens; o->look_sens = clampi(o->look_sens + d, 1, 10); return old != o->look_sens;
    case I_INVERT: o->invert_y = !o->invert_y; return 1;
    case I_BOB: o->head_bob = !o->head_bob; return 1;
    case I_RUN: o->run_mode = !o->run_mode; return 1;
    case I_MUSIC: old = o->music_vol; o->music_vol = clampi(o->music_vol + d, 0, 10); return old != o->music_vol;
    case I_SFX: old = o->sfx_vol; o->sfx_vol = clampi(o->sfx_vol + d, 0, 10); return old != o->sfx_vol;
    case I_DIST: old = o->view; o->view = clampi(o->view + d, 0, VIEW_N - 1); return old != o->view;
    case I_VCAP: old = o->vcap; o->vcap = clampi(o->vcap + d, VIEW_CAP_MIN, VIEW_MAX); return old != o->vcap;
    case I_VTARGET: o->vtarget = !o->vtarget; return 1;
    case I_RES: old = o->res; o->res = clampi(o->res + d, 0, 3); return old != o->res;
    case I_ASPECT: o->aspect = !o->aspect; return 1;
    case I_SCALE: o->scaling = (o->scaling + (d > 0 ? 1 : 2)) % 3; return 1;
    case I_FOG: o->fogmode = (o->fogmode + (d > 0 ? 1 : 2)) % 3; return 1;
    case I_SKY: o->skyfx = !o->skyfx; return 1;
    case I_SNAP: o->snap = !o->snap; return 1;
    case I_MODELS: o->models = !o->models; return 1;
    case I_FPS: o->show_fps = !o->show_fps; return 1;
    case I_THREADS: old = o->threads; o->threads = clampi(o->threads + d, 1, 4); return old != o->threads;
    case I_ADDL: o->addl = !o->addl; return 1;
    case I_AFFINE: old = o->affine; o->affine = clampi(o->affine + d, 0, 4); return old != o->affine;
    case I_FOOT: o->footsteps = !o->footsteps; return 1;
    case I_SCHEME: o->scheme = !o->scheme; return 1;
    case I_DEADZONE: old = o->deadzone; o->deadzone = clampi(o->deadzone + d, 0, 8); return old != o->deadzone;
    case I_SPDAYS: o->specialdays = !o->specialdays; return 1;
    case I_INTRO: o->intro = !o->intro; return 1;
  }
  return 0;
}
static int is_option(int id) { return (id >= I_SENS && id <= I_THREADS && id != I_GFX && id != I_BACK) || id == I_ADDL || id == I_AFFINE ||
                                      id == I_FOOT || id == I_SCHEME || id == I_DEADZONE || id == I_SPDAYS || id == I_INTRO || id == I_VCAP || id == I_VTARGET; }
static int is_page(int s) { return s == S_GAMEPLAY || s == S_CONTROLS || s == S_AUDIO || s == S_GRAPHICS || s == S_DREAMOPT; }

int menu_nav(Menu *m, int nav, const MenuCtx *c, int *arg) {
  Item it[MAX_ITEMS]; int n = build(m, c, it);
  int *sel = &m->sel[m->screen];
  if (m->screen == S_CHART) {                       /* the chart is a picture: any of A, B, + closes it */
    if (nav == NAV_OK || nav == NAV_BACK || nav == NAV_START) { pop(m); audio_sfx(SFX_PRESS); }
    return MA_NONE;
  }
  if (n <= 0) { if (nav == NAV_BACK || nav == NAV_START) { pop(m); audio_sfx(SFX_PRESS); } return MA_NONE; }
  fix_sel(it, n, sel);
  if (m->screen == S_DREAMS && nav >= NAV_UP && nav <= NAV_RIGHT) {   /* two columns: up/down inside one, left/right across */
    int split = dream_split(c, it, n), in1 = *sel >= split, c0 = in1 ? split : 0, c1 = in1 ? n : split;
    if (nav == NAV_UP || nav == NAV_DOWN) { int len = c1 - c0, d = nav == NAV_UP ? -1 : 1; *sel = c0 + (*sel - c0 + d + len) % len; }
    else {
      if (split <= 0 || split >= n) return MA_NONE;
      int row = *sel - c0, o0 = in1 ? 0 : split, ol = in1 ? split : n - split;
      *sel = o0 + (row < ol ? row : ol - 1);
    }
    audio_sfx(SFX_HOVER);
    return MA_NONE;
  }
  switch (nav) {
    case NAV_UP: case NAV_DOWN: {
      int d = nav == NAV_UP ? -1 : 1;
      *sel = (*sel + d + n) % n;
      for (int k = 0; k < n && it[*sel].id == I_SEP; k++) *sel = (*sel + d + n) % n;   /* skip the headings */
      audio_sfx(SFX_HOVER);
      return MA_NONE; }
    case NAV_LEFT: case NAV_RIGHT:
      if (is_option(it[*sel].id) && change(it[*sel].id, nav == NAV_LEFT ? -1 : 1)) {
        if (it[*sel].id == I_SFX) { audio_set_volume(g_opt.music_vol, g_opt.sfx_vol); sfx_soundtest(); } else audio_sfx(SFX_HOVER);
        return MA_OPTIONS; }
      return MA_NONE;
    case NAV_START:
      if (m->screen == S_PAUSE) { audio_sfx(SFX_PRESS); return MA_RESUME; }
      if (m->screen == S_MAIN) return MA_NONE;
      /* fall through: + acts as B in the sub screens */
    case NAV_BACK: {
      if (m->screen == S_PAUSE) { audio_sfx(SFX_PRESS); return MA_RESUME; }
      if (m->screen == S_MAIN) return MA_NONE;
      audio_sfx(SFX_PRESS);
      int from = pop(m);
      return from == S_OPTIONS ? MA_OPTIONS_DONE : MA_NONE; }
    case NAV_OK: {
      int id = it[*sel].id;
      audio_sfx(SFX_PRESS);
      if (id >= I_DREAM0) { *arg = id - I_DREAM0; return MA_DREAM; }
      if (id >= I_BIND0 && id < I_BIND0 + ACT_N) { m->capture = id - I_BIND0 + 1; m->cap_t = 5.0f; return MA_NONE; }
      if (is_option(id)) { int ch = change(id, 1); if (ch && id == I_SFX) { audio_set_volume(g_opt.music_vol, g_opt.sfx_vol); sfx_soundtest(); } return ch ? MA_OPTIONS : MA_NONE; }
      switch (id) {
        case I_CONTINUE: return MA_CONTINUE;
        case I_NEW: if (g_save.has_game && (g_save.day > 1 || g_save.nhist > 0)) { push(m, S_CONFIRM_NEW); return MA_NONE; } return MA_NEW_GAME;
        case I_DREAMS: { push(m, S_DREAMS); int d = c->in_game && c->cur_dream >= 0 ? c->cur_dream : g_save.last_dream > 0 ? g_save.last_dream : 0;
          if (c->dl && d >= c->dl->norig && !g_opt.addl) d = 0;
          m->sel[S_DREAMS] = d; return MA_NONE; }
        case I_CHART: push(m, S_CHART); return MA_CHART_OPEN;
        case I_FLASHBACK: return MA_FLASHBACK;
        case I_JOURNAL: return MA_JOURNAL;
        case I_CREDITS: return MA_CREDITS;
        case I_OPTIONS: push(m, S_OPTIONS); return MA_NONE;
        case I_CAT_GAME: push(m, S_GAMEPLAY); return MA_NONE;
        case I_CAT_AUDIO: push(m, S_AUDIO); return MA_NONE;
        case I_CAT_DREAMS: push(m, S_DREAMOPT); return MA_NONE;
        case I_GFX: push(m, S_GRAPHICS); return MA_NONE;
        case I_CONTROLS: push(m, S_CONTROLS); return MA_NONE;
        case I_RESETBIND: input_default_bindings(g_opt.bind); return MA_OPTIONS;
        case I_QUIT: return MA_QUIT;
        case I_RESUME: return MA_RESUME;
        case I_TITLE: return MA_TITLE;
        case I_SAVEQUIT: return MA_SAVE_QUIT;
        case I_YES: pop(m); return MA_NEW_GAME;
        case I_NO: pop(m); return MA_NONE;
        case I_BACK: { int from = pop(m); return from == S_OPTIONS ? MA_OPTIONS_DONE : MA_NONE; }
      }
      return MA_NONE; }
  }
  return MA_NONE;
}

/* GamePad touch: a value control's left half lowers, right half raises; a tap on another row selects it, a tap on the
 * selected row (or on any row of the title / pause / hub / dream lists) activates it; footer button hints act as A / B */
int menu_tap(Menu *m, int x, int y, const MenuCtx *c, int *arg) {
  if (m->capture) return MA_NONE;
  if (m->screen == S_CHART) return menu_nav(m, NAV_BACK, c, arg);
  for (int k = m->nhit - 1; k >= 0; k--) {
    const MenuHit *h = &m->hit[k];
    if (x < h->x || x >= h->x + h->w || y < h->y || y >= h->y + h->h) continue;
    if (h->item == -1) return menu_nav(m, NAV_BACK, c, arg);
    if (h->item == -2) return menu_nav(m, NAV_OK, c, arg);
    Item it[MAX_ITEMS]; int n = build(m, c, it);
    if (h->item < 0 || h->item >= n || it[h->item].id == I_SEP) return MA_NONE;
    int *sel = &m->sel[m->screen], was = *sel == h->item;
    *sel = h->item;
    if (h->ctl && x >= h->ctl && is_option(it[h->item].id)) return menu_nav(m, x < h->ctl + 46 ? NAV_LEFT : NAV_RIGHT, c, arg);
    if (is_page(m->screen) && !was) { audio_sfx(SFX_HOVER); return MA_NONE; }
    return menu_nav(m, NAV_OK, c, arg);
  }
  return MA_NONE;
}

int menu_capturing(const Menu *m) { return m->capture > 0; }
int menu_capture(Menu *m, int button) {
  int a = m->capture - 1; m->capture = 0;
  if (a < 0 || a >= ACT_N || button < 0 || button >= BN) return MA_NONE;
  for (int k = 0; k < ACT_N; k++) if (k != a && g_opt.bind[k] == button) g_opt.bind[k] = g_opt.bind[a];   /* taken: swap */
  g_opt.bind[a] = button;
  m->rep_dir = button >= BLEFT && button <= BDOWN ? (button == BLEFT ? NAV_LEFT : button == BUP ? NAV_UP : button == BRIGHT ? NAV_RIGHT : NAV_DOWN) : -1;   /* no auto-repeat from the held d-pad */
  m->rep_t = 1e9f;
  audio_sfx(SFX_PRESS);
  return MA_OPTIONS;
}

int menu_hold(Menu *m, int dir, float dt, const MenuCtx *c, int *arg) {
  if (m->capture) { if ((m->cap_t -= dt) <= 0) { m->capture = 0; audio_sfx(SFX_HOVER); } return MA_NONE; }
  if (dir < 0) { m->rep_dir = -1; return MA_NONE; }
  if (dir != m->rep_dir) { m->rep_dir = dir; m->rep_t = 0.4f; return menu_nav(m, dir, c, arg); }   /* first press, then repeat */
  m->rep_t -= dt;
  if (m->rep_t <= 0) { m->rep_t += 0.11f; return menu_nav(m, dir, c, arg); }
  return MA_NONE;
}

/* ---------------------------------------------------------------- drawing */
#define C_SEL   0xFFFFE04Au      /* selected: the faces' yellow */
#define C_ITEM  0xFFB4B8DCu      /* the rings' lavender */
#define C_DIM   0xFF6A6488u
#define C_HEAD  0xFFFFFFFFu
#define C_SECT  0xFFD8B050u      /* section headings: a darker gold */
#define C_PANEL 0xFF0C0624u
#define C_EDGE  0xFF3A3070u
#define C_BAR   0xFF6050A0u

uint32_t menu_hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

/* content of the current screen during a screen change: slid by s_ox and faded in (s_a 0..256, towards the navy) */
static int s_ox, s_a = 256;
static uint32_t fadec(uint32_t c) {
  if (s_a >= 256) return c;
  int r = ((int)(c >> 16 & 255) * s_a + KEY_R * (256 - s_a)) >> 8, g = ((int)(c >> 8 & 255) * s_a + KEY_G * (256 - s_a)) >> 8,
      b = ((int)(c & 255) * s_a + KEY_B * (256 - s_a)) >> 8;
  return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}
static void txt(Frame *f, int x, int y, const char *s, uint32_t c, int sc) { if (s_a >= 48) ui_text(f, x + s_ox, y, s, fadec(c), sc); }
static void txtc(Frame *f, int cx, int y, const char *s, uint32_t c, int sc) { txt(f, cx - ui_text_w(s, sc) / 2, y, s, c, sc); }
static void txtr(Frame *f, int rx, int y, const char *s, uint32_t c, int sc) { txt(f, rx - ui_text_w(s, sc), y, s, c, sc); }
static void rect(Frame *f, int x, int y, int w, int h, uint32_t c, int a) { ui_rect(f, x + s_ox, y, w, h, c, a * s_a >> 8); }
static void faceo(Frame *f, int x, int y) { if (s_a >= 128) face_icon(f, x + s_ox, y); }
static void add_hit(Menu *m, int x, int y, int w, int h, int item, int ctl) {
  if (m->nhit >= MENU_MAXI) return;
  MenuHit *k = &m->hit[m->nhit++]; k->x = (short)x; k->y = (short)y; k->w = (short)w; k->h = (short)h; k->item = (short)item; k->ctl = (short)ctl;
}
static float ease(float *v, float target, float k) { *v += (target - *v) * k; if (fabsf(target - *v) < 0.05f) *v = target; return *v; }

/* a translucent dark panel with a lit top edge, a frame line and a drop shadow (right and bottom) */
static void panel(Frame *f, int x, int y, int w, int h, int a) {
  rect(f, x + w, y + 3, 3, h, 0xFF000000u, a / 2); rect(f, x + 3, y + h, w - 3, 3, 0xFF000000u, a / 2);
  rect(f, x, y, w, h, C_PANEL, a);
  rect(f, x, y, w, 1, 0xFF8C84C0u, a * 3 / 4);
  rect(f, x, y + h - 1, w, 1, C_EDGE, a); rect(f, x, y + 1, 1, h - 2, C_EDGE, a); rect(f, x + w - 1, y + 1, 1, h - 2, C_EDGE, a);
}
/* the selection: a lilac bar with a gold edge on the left */
static void sel_bar(Frame *f, int x, int y, int w, int h) {
  rect(f, x, y, w, h, C_BAR, 120);
  rect(f, x, y, 2, h, C_SEL, 230);
  rect(f, x + 2, y, w - 2, 1, 0xFFA898E0u, 70);
}
/* a numeric setting: track, fill and knob (v of max) */
static void slider(Frame *f, int x, int y, int w, int v, int max, int sel) {
  int fw = max > 0 ? w * v / max : 0;
  rect(f, x, y + 2, w, 3, 0xFF2A2250u, 255);
  rect(f, x, y + 2, fw, 3, sel ? C_SEL : C_ITEM, 255);
  for (int k = 1; k < max; k++) rect(f, x + w * k / max, y + 2, 1, 3, 0xFF0C0624u, 120);   /* steps */
  rect(f, x + fw - 1, y, 3, 7, sel ? 0xFFFFFFFFu : C_ITEM, 255);
}
/* a value pill: "<  VALUE  >" (arrows only for values that left/right change) */
static void pill(Frame *f, int x, int y, int w, const char *v, int sel, int arrows, int on) {
  rect(f, x + 1, y - 2, w - 2, 11, sel ? 0xFF3A2C78u : 0xFF1E1644u, 220); rect(f, x, y - 1, 1, 9, sel ? 0xFF3A2C78u : 0xFF1E1644u, 220);
  rect(f, x + w - 1, y - 1, 1, 9, sel ? 0xFF3A2C78u : 0xFF1E1644u, 220);
  if (arrows) { txt(f, x + 3, y, "<", sel ? C_SEL : C_DIM, 1); txt(f, x + w - 8, y, ">", sel ? C_SEL : C_DIM, 1); }
  txtc(f, x + w / 2, y, v, sel ? C_SEL : on ? C_ITEM : C_DIM, 1);
}
static void scrollbar(Frame *f, int x, int y, int h, float top, int vis, int total) {
  if (total <= vis) return;
  rect(f, x, y, 2, h, 0xFF2A2250u, 255);
  int th = h * vis / total; if (th < 6) th = 6;
  int ty = y + (int)((h - th) * top / (float)(total - vis) + 0.5f);
  rect(f, x, ty, 2, th, C_ITEM, 255);
}
/* a button cap with its letter (A, B, +, <>): returns its width */
static int btn(Frame *f, int x, int y, const char *k) {
  int w = ui_text_w(k, 1) + 6;
  ui_rect(f, x + 1, y - 2, w - 2, 11, 0xFFB4B8DCu, 235); ui_rect(f, x, y - 1, 1, 9, 0xFFB4B8DCu, 235); ui_rect(f, x + w - 1, y - 1, 1, 9, 0xFFB4B8DCu, 235);
  glyphs(f, x + 3, y, k, 0xFF140C30u, 1);
  return w;
}
/* button hints right-aligned at rx: pairs of (button, label), NULL-terminated; registers taps for A / B */
static void hints(Frame *f, Menu *m, int rx, int y, const char *const *hk) {
  int w = 0, np = 0;
  for (; hk[np * 2]; np++) w += ui_text_w(hk[np * 2], 1) + 6 + 3 + ui_text_w(hk[np * 2 + 1], 1) + (hk[np * 2 + 2] ? 10 : 0);
  int x = rx - w;
  for (int k = 0; k < np; k++) {
    int x0 = x; x += btn(f, x, y, hk[k * 2]) + 3;
    ui_text(f, x, y, hk[k * 2 + 1], C_ITEM, 1); x += ui_text_w(hk[k * 2 + 1], 1) + 10;
    if (m && (!strcmp(hk[k * 2], "B") || !strcmp(hk[k * 2], "A"))) add_hit(m, x0 - 2, y - 4, x - x0, 15, hk[k * 2][0] == 'B' ? -1 : -2, 0);
  }
}
/* the footer: a band with the highlighted item's description and a line of context (left) + button hints (right) */
static void footer(Frame *f, Menu *m, const char *desc, const char *left, const char *const *hk) {
  int y0 = f->h - 34;
  ui_rect(f, 0, y0, f->w, 34, 0xFF06031Au, 185);
  for (int x = 0; x < f->w; x++) put_px(f, x, y0, (x / 2) & 1 ? 0xFF8C84C0u : 0xFF4A4078u);
  if (desc && *desc) text_c(f, f->w / 2, y0 + 7, desc, C_SEL, 1);
  for (int x = 24; x < f->w - 24; x++) if ((x & 3) == 0) put_px(f, x, y0 + 19, 0xFF2E2458u);
  if (left && *left) ui_text(f, 8, f->h - 11, left, C_DIM, 1);
  if (hk) hints(f, m, f->w - 8, f->h - 11, hk);
}

void menu_backdrop(Frame *f, float t) {
  for (int y = 0; y < f->h; y++) {                      /* the title art's navy, slowly breathing towards purple at the bottom */
    float k = (float)y / f->h, br = 0.08f * sinf(t * 0.5f + k * 3.0f);
    int r = (int)(KEY_R + k * 10 + br * 20), g = (int)(KEY_G - k * 4), b = (int)(KEY_B - k * 28 + br * 18);
    uint32_t col = 0xFF000000u | ((uint32_t)(r < 0 ? 0 : r) << 16) | ((uint32_t)(g < 0 ? 0 : g) << 8) | (uint32_t)(b < 0 ? 0 : b);
    for (int x = 0; x < f->w; x++) f->color[y * f->w + x] = col;
  }
  int W = f->w + 11, ns = 70 * f->w / 320;
  for (int i = 0; i < ns; i++) {                        /* twinkling stars drifting left */
    uint32_t h = menu_hash((uint32_t)i * 7919u + 17u);
    float sp = 2.0f + (h >> 24) / 40.0f;
    int x = (int)((h % (unsigned)W) - t * sp); x = ((x % W) + W) % W - 5; int y = (int)((h >> 9) % (unsigned)f->h);
    float tw = 0.5f + 0.5f * sinf(t * (1.0f + (h & 7) * 0.4f) + (float)(h & 255));
    int v = (int)(60 + 170 * tw * tw);
    uint32_t col = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)(v * 7 / 8) << 8) | (uint32_t)(v < 200 ? v + 55 : 255);
    put_px(f, x, y, col);
    if ((h & 15) == 0 && tw > 0.7f) { put_px(f, x - 1, y, col); put_px(f, x + 1, y, col); put_px(f, x, y - 1, col); put_px(f, x, y + 1, col); }
  }
}
static void background(Frame *f, const MenuCtx *c, float t) {
  if (c->bg) {                                          /* pause: the frozen game, darkened and tinted navy */
    int bw = c->bg_w > 0 ? c->bg_w : MENU_W;
    for (int y = 0; y < f->h; y++) for (int x = 0; x < f->w; x++) {
      uint32_t p = c->bg[y * bw + x * bw / f->w];
      int r = (int)(p >> 16 & 255) * 90 / 256 + 10, g = (int)(p >> 8 & 255) * 90 / 256 + 4, b = (int)(p & 255) * 90 / 256 + 30;
      f->color[y * f->w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return;
  }
  menu_backdrop(f, t);
}
void menu_header(Frame *f, const char *title, float t) {
  int w = ui_text_w(title, 2), bob = (int)(sinf(t * 3.0f) * 1.5f);
  ui_rect(f, 0, 0, f->w, 28, 0xFF06031Au, 110);
  text_c(f, f->w / 2, 8, title, C_HEAD, 2);
  face_icon(f, f->w / 2 - w / 2 - 18, 10 + bob); face_icon(f, f->w / 2 + w / 2 + 8, 10 - bob);
  for (int x = 0; x < f->w; x++) put_px(f, x, 28, (x / 2) & 1 ? 0xFF8C84C0u : 0xFF4A4078u);
}

/* title / pause / dialog lists: centred items, the highlighted one at 2x between two bobbing faces; rows ease into place */
static void draw_grow(Frame *f, Menu *m, const Item *it, int n, int sel, int cx, int y0, int rs, int rb, float t, float k) {
  int y = y0;
  for (int i = 0; i < n; i++) {
    float target = (float)y; y += i == sel ? rb : rs;
    if (m->snap) m->iy[i] = target; else ease(&m->iy[i], target, k);
    int yy = (int)(m->iy[i] + 0.5f);
    if (i == sel) {
      int w = ui_text_w(it[i].label, 2), ty = yy + (rb - 14) / 2;
      float p = 0.5f + 0.5f * sinf(t * 3.0f);           /* a slow glow on the gold */
      uint32_t col = 0xFF000000u | 0xFF0000u | (uint32_t)(0xE0 + (int)(0x1F * p)) << 8 | (uint32_t)(0x4A + (int)(0x60 * p));
      rect(f, cx - w / 2 - 26, ty - 3, w + 52, 20, C_BAR, 80);
      rect(f, cx - w / 2 - 26, ty - 3, w + 52, 1, 0xFFA898E0u, 60); rect(f, cx - w / 2 - 26, ty + 16, w + 52, 1, 0xFF0C0624u, 120);
      txt(f, cx - w / 2, ty, it[i].label, col, 2);
      int wob = (int)(sinf(t * 4.0f) * 1.5f);
      faceo(f, cx - w / 2 - 19 + wob, ty + 2); faceo(f, cx + w / 2 + 9 - wob, ty + 2);
      add_hit(m, cx - w / 2 - 26, ty - 3, w + 52, 20, i, 0);
    } else {
      txtc(f, cx, yy + (rs - 7) / 2, it[i].label, C_ITEM, 1);
      int w = ui_text_w(it[i].label, 1); add_hit(m, cx - w / 2 - 20, yy, w + 40, rs, i, 0);
    }
  }
}

/* a settings page: grouped rows in a panel, value pill / slider on the right, scrolling with a scrollbar */
static void draw_page(Frame *f, Menu *m, const Item *it, int n, int sel, float k) {
  int pw = f->w - 24 < 360 ? f->w - 24 : 360, px0 = (f->w - pw) / 2, top = 33, bottom = f->h - 40, row = 14;
  int vis = (bottom - top - 6) / row, scrolls = n > vis;
  panel(f, px0, top, pw, bottom - top, 200);
  float tgt = m->scroll[0];
  if (sel < tgt + 1) tgt = (float)(sel - 1);
  if (sel > tgt + vis - 2) tgt = (float)(sel - vis + 2);
  if (tgt > n - vis) tgt = (float)(n - vis);
  if (tgt < 0) tgt = 0;
  if (m->snap) m->scroll[0] = tgt; else ease(&m->scroll[0], tgt, k);
  float sc = m->scroll[0];
  int ry0 = top + 4, rx1 = px0 + pw - (scrolls ? 12 : 8), cw = 96, ctl = rx1 - cw;
  float by = ry0 + (sel - sc) * row;
  if (m->snap) m->bar_y = by; else ease(&m->bar_y, by, k);
  if (m->bar_y >= top && m->bar_y + row <= bottom) sel_bar(f, px0 + 4, (int)(m->bar_y + 0.5f), rx1 - px0 - 2, row - 1);
  for (int i = 0; i < n; i++) {
    int y = (int)(ry0 + (i - sc) * row + 0.5f), s = i == sel, id = it[i].id;
    if (y < top + 2 || y + row > bottom) continue;
    if (id == I_SEP) {                                   /* section heading */
      int w = ui_text_w(it[i].label, 1);
      txt(f, px0 + 10, y + 4, it[i].label, C_SECT, 1);
      for (int x = px0 + 16 + w; x < rx1; x++) if ((x & 1) == 0) rect(f, x, y + 7, 1, 1, 0xFF6A5A30u, 255);
      continue;
    }
    if (s) faceo(f, px0 + 8, y + 2);
    txt(f, px0 + 22, y + 3, it[i].label, s ? C_SEL : C_ITEM, 1);
    int bind = id >= I_BIND0 && id < I_BIND0 + ACT_N;
    if (it[i].slider >= 0) {
      slider(f, ctl + 4, y + 3, 62, it[i].slider, it[i].smax, s);
      txtr(f, rx1 - 2, y + 3, it[i].value, s ? C_SEL : C_ITEM, 1);
    } else if (it[i].value[0]) {
      int on = strcmp(it[i].value, "OFF") != 0;
      pill(f, ctl, y + 3, cw, it[i].value, s, !bind, on);
    } else txtr(f, rx1 - 4, y + 3, s ? "A >" : ">", s ? C_SEL : C_DIM, 1);   /* an action (RESET BUTTONS) */
    add_hit(m, px0, y, pw, row, i, is_option(id) ? ctl : 0);
  }
  if (scrolls) scrollbar(f, px0 + pw - 7, top + 4, bottom - top - 8, sc, vis, n);
}

static const char *const HK_PAGE[] = { "<>", "CHANGE", "B", "BACK", NULL };
static const char *const HK_OPEN[] = { "A", "OPEN", "B", "BACK", NULL };

static void draw_chart(Frame *f, const MenuCtx *c, float t) {
  int ox = (f->w - 320) / 2; if (ox < 0) ox = 0;
  if (f->w > 320) menu_backdrop(f, t);
  if (chart_px && chart_w == 320 && chart_h == 240 && f->h == 240) {
    for (int y = 0; y < 240; y++) memcpy(f->color + y * f->w + ox, chart_px + y * 320, 320 * 4);
  } else {                                              /* no art: a plain 19x19 grid */
    fill(f, ox, 0, 320, f->h, 0xFF18104Au);
    for (int gy = 0; gy < 19; gy++) for (int gx = 0; gx < 19; gx++) fill(f, ox + 65 + gx * 10, 16 + gy * 10, 9, 9, 0xFF9C8B00u);
    text_c(f, ox + 160, 4, "UPPER", C_HEAD, 1); text_c(f, ox + 160, 230, "DOWNER", C_HEAD, 1);
    ui_text(f, ox + 8, 116, "STATIC", C_HEAD, 1); ui_text(f, ox + 270, 116, "DYNAMIC", C_HEAD, 1);
  }
  if (ox > 0) { ui_rect(f, ox - 1, 0, 1, f->h, 0xFF000000u, 120); ui_rect(f, ox + 320, 0, 1, f->h, 0xFF000000u, 120); }
  /* HGRAPH.TIM's grid: 19x19 cells of 10x10 pixels from (65,16); x = static (-9, left) .. dynamic (+9, right),
   * y = upper (+9, top) .. downer (-9, bottom), like Revamped's UIGraphSquare. Days get a shade darker every 10 days
   * (UIGraph.DAYS_PER_SHADE) and the most recent one flashes red. */
  int n = g_save.nhist;
  for (int i = 0; i < n; i++) {
    int ago = n - 1 - i, mod = 5 - ago / 10; if (mod < 1) mod = 1;
    int v = 51 * mod; uint32_t col = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
    if (ago == 0 && fmodf(t, 0.25f) > 0.125f) col = 0xFFFF0000u;
    int x = ox + 65 + (g_save.hist[i][0] + 9) * 10, y = 16 + (9 - g_save.hist[i][1]) * 10;
    fill(f, x + 1, y + 1, 8, 8, col);
  }
  if (c->in_game && c->cur_has && fmodf(t, 0.6f) > 0.2f) {   /* where today is heading (pause only) */
    int x = ox + 65 + (c->cur_gx + 9) * 10, y = 16 + (9 - c->cur_gy) * 10;
    for (int k = 0; k < 10; k++) { put_px(f, x + k, y, 0xFFFFFFFFu); put_px(f, x + k, y + 9, 0xFFFFFFFFu); put_px(f, x, y + k, 0xFFFFFFFFu); put_px(f, x + 9, y + k, 0xFFFFFFFFu); }
  }
  char b[48];
  snprintf(b, sizeof b, "%d DAY%s CHARTED", n, n == 1 ? "" : "S");
  ui_rect(f, ox, f->h - 10, ui_text_w(b, 1) + 10, 10, 0xFF000000u, 140);
  ui_text(f, ox + 4, f->h - 9, b, C_ITEM, 1);
  if (ox >= 48) { static const char *const hk[] = { "B", "BACK", NULL }; hints(f, NULL, f->w - 6, f->h - 11, hk); }
}

/* DREAM SELECT: one column per dream set (ORIGINAL DREAMS, ADDITIONAL DREAMS), each scrolling on its own if needed */
static void draw_dreams(Frame *f, Menu *m, const MenuCtx *c, const Item *it, int n, int sel, float k, float t) {
  int split = dream_split(c, it, n), ncol = split < n && split > 0 ? 2 : 1;
  int gap = 6, colw = ncol == 2 ? (f->w - 12 - gap) / 2 : (f->w - 24 < 230 ? f->w - 24 : 230);
  int x0 = ncol == 2 ? 6 : (f->w - colw) / 2, top = 31, bottom = f->h - 38, row = 11, ry0 = top + 15;
  int vis = (bottom - 2 - ry0) / row;
  for (int col = 0; col < ncol; col++) {
    int a = col == 0 ? 0 : split, b = col == 0 ? split : n, len = b - a, cx = x0 + col * (colw + gap), seen = 0;
    for (int i = a; i < b; i++) seen += it[i].id - I_DREAM0 < SAVE_MAX_DREAMS && g_save.visited[it[i].id - I_DREAM0];
    panel(f, cx, top, colw, bottom - top, 200);
    txt(f, cx + 7, top + 4, col == 0 ? "ORIGINAL DREAMS" : "ADDITIONAL DREAMS", C_SECT, 1);
    char cnt[16]; snprintf(cnt, sizeof cnt, "%d/%d", seen, len); txtr(f, cx + colw - 7, top + 4, cnt, C_DIM, 1);
    for (int x = cx + 6; x < cx + colw - 6; x++) if ((x & 1) == 0) rect(f, x, top + 12, 1, 1, 0xFF6A5A30u, 255);
    int here_col = sel >= a && sel < b;
    float tgt = m->scroll[col], sr = here_col ? (float)(sel - a) : tgt + 1;
    if (sr < tgt + 1) tgt = sr - 1;
    if (sr > tgt + vis - 2) tgt = sr - vis + 2;
    if (tgt > len - vis) tgt = (float)(len - vis);
    if (tgt < 0) tgt = 0;
    if (m->snap) m->scroll[col] = tgt; else ease(&m->scroll[col], tgt, k);
    float sc = m->scroll[col];
    int sb = len > vis, rx1 = cx + colw - (sb ? 8 : 4);
    if (here_col) {
      float by = ry0 + (sel - a - sc) * row;
      if (m->snap) m->bar_y = by; else ease(&m->bar_y, by, k);
      sel_bar(f, cx + 3, (int)(m->bar_y + 0.5f), rx1 - cx - 3, row);
    }
    for (int i = a; i < b; i++) {
      int y = (int)(ry0 + (i - a - sc) * row + 0.5f), s = i == sel, d = it[i].id - I_DREAM0;
      if (y < top + 13 || y + row > bottom) continue;
      int vis_ = d < SAVE_MAX_DREAMS && g_save.visited[d], here = c->in_game && d == c->cur_dream;
      if (s) faceo(f, cx + 5, y + 1 + (int)(sinf(t * 4.0f) * 0.8f));
      if (vis_) txt(f, cx + 17, y + 2, "*", s ? C_SEL : 0xFFFFC040u, 1);
      txt(f, cx + 24, y + 2, it[i].label, s ? C_SEL : here ? C_HEAD : vis_ ? C_ITEM : C_DIM, 1);
      if (here) txt(f, rx1 - 7, y + 2, "<", C_SEL, 1);
      add_hit(m, cx, y, colw, row, i, 0);
    }
    if (sb) scrollbar(f, cx + colw - 6, ry0, vis * row, sc, vis, len);
  }
}

void menu_draw(Menu *m, Frame *f, const MenuCtx *c, float dt) {
  m->t += dt; float t = m->t;
  m->tr += dt / 0.22f;
  if (m->tr > 1.0f) m->tr = 1.0f;
  float e = 1.0f - (1.0f - m->tr) * (1.0f - m->tr) * (1.0f - m->tr);   /* ease out */
  float k = 1.0f - expf(-dt * 18.0f);                                   /* selection / scroll easing per frame */
  Item it[MAX_ITEMS]; int n = build(m, c, it);
  int *selp = &m->sel[m->screen]; if (n > 0) fix_sel(it, n, selp);
  int sel = n > 0 ? *selp : 0;
  m->nhit = 0;
  if (m->screen == S_CHART) { draw_chart(f, c, t); m->snap = 0; return; }
  background(f, c, t);
  char b[96];
  const char *desc = n > 0 && sel < n ? it[sel].hint : NULL;
  s_ox = (int)(m->tr_dir * (1.0f - e) * 28.0f); s_a = (int)(e * 256.0f);   /* the content slides and fades in */
  switch (m->screen) {
    case S_MAIN: {
      static const int faces[4][2] = { {0, 0}, {1, 0}, {2, 0}, {0, 1} };
      int cx = f->w / 2;
      s_ox = 0;
      /* three rings: the LSD logo in the middle, the faces cycling (cross-faded) on both sides */
      for (int side = 0; side < 2; side++) {
        float ph = t / 2.4f + side * 2.0f; int kk = (int)ph; float fr = ph - kk;
        int a = fr > 0.8f ? (int)((1.0f - (fr - 0.8f) / 0.2f) * 256) : 256;
        int rx = side ? cx + 80 : cx - 80, ry = 41 + (int)(sinf(t * 1.3f + side * 2.1f) * 2.0f);
        ring(f, faces[kk & 3][0], faces[kk & 3][1], rx, ry, a);
        if (a < 256) ring(f, faces[(kk + 1) & 3][0], faces[(kk + 1) & 3][1], rx, ry, 256 - a);
      }
      ring(f, 1, 1, cx, 39 + (int)(sinf(t * 0.9f) * 1.5f), 256);
      if (!title_px) text_c(f, cx, 32, "LSD", C_SEL, 3);
      text_c(f, cx, 80, "DREAM EMULATOR", C_HEAD, 2);
      text_c(f, cx, 97, "LSD: REVAMPED FOR WII U", C_DIM, 1);
      int rs = 11, rb = 19, total = (n - 1) * rs + rb, area0 = 110, area1 = f->h - 37;
      if (total > area1 - area0) { rs = 10; rb = 18; total = (n - 1) * rs + rb; }
      int y0 = area0 + (area1 - area0 - total) / 2;

      draw_grow(f, m, it, n, sel, cx, y0, rs, rb, t, k);
      if (g_save.has_game) snprintf(b, sizeof b, "DAY %d", g_save.day); else snprintf(b, sizeof b, "%s", save_path() ? "" : "NO SD CARD: NOT SAVING");
      s_ox = 0; s_a = 256;
      static const char *const hk[] = { "A", "SELECT", NULL };
      footer(f, m, desc, b, hk);
      break; }
    case S_PAUSE: {
      int pw = f->w - 24 < 300 ? f->w - 24 : 300, px0 = (f->w - pw) / 2, cy = 33;
      panel(f, px0, cy, pw, 38, 200);                  /* the day / dream / song card */
      txt(f, px0 + 10, cy + 5, "DAY", C_DIM, 1);
      snprintf(b, sizeof b, "%d", c->day); txt(f, px0 + 10, cy + 16, b, C_SEL, 2);
      int dx = px0 + 10 + (ui_text_w(b, 2) > 30 ? ui_text_w(b, 2) : 30) + 10;
      rect(f, dx - 6, cy + 5, 1, 28, C_EDGE, 255);
      const char *dn = c->dl && c->cur_dream >= 0 && c->cur_dream < c->dl->n ? c->dl->d[c->cur_dream].name : "";
      txt(f, dx, cy + 5, "DREAM", C_DIM, 1); txt(f, dx + 36, cy + 5, dn, C_HEAD, 1);
      txt(f, dx, cy + 16, "MUSIC", C_DIM, 1); txt(f, dx + 36, cy + 16, c->song ? c->song : "-", C_ITEM, 1);
      { int vis = 0, tot = 0;
        if (c->dl) for (int i = 0; i < c->dl->n && i < SAVE_MAX_DREAMS; i++) if (i < c->dl->norig || g_opt.addl) { tot++; vis += g_save.visited[i]; }
        snprintf(b, sizeof b, "VISITED %d/%d", vis, tot); txt(f, dx, cy + 27, b, C_DIM, 1); }
      draw_grow(f, m, it, n, sel, f->w / 2, 80, 15, 21, t, k);
      s_ox = 0; s_a = 256;
      menu_header(f, "PAUSED", t);
      static const char *const hk[] = { "A", "SELECT", "B", "RESUME", NULL };
      footer(f, m, desc, NULL, hk);
      break; }
    case S_DREAMS: {
      draw_dreams(f, m, c, it, n, sel, k, t);
      s_ox = 0; s_a = 256;
      menu_header(f, "DREAM SELECT", t);
      int seen = 0, total = n;
      for (int i = 0; i < n; i++) seen += it[i].id - I_DREAM0 < SAVE_MAX_DREAMS && g_save.visited[it[i].id - I_DREAM0];
      char d2[96] = "";
      if (n > 0) { int d = it[sel].id - I_DREAM0, v = d < SAVE_MAX_DREAMS && g_save.visited[d];
        snprintf(d2, sizeof d2, "%s: %s", it[sel].label, c->in_game && d == c->cur_dream ? "YOU ARE HERE" : v ? "VISITED" : "NOT VISITED YET"); }
      snprintf(b, sizeof b, "* %d/%d", seen, total);
      int two = dream_split(c, it, n) < n;
      static const char *const hk2[] = { "<>", "SET", "A", "GO", "B", "BACK", NULL };
      static const char *const hk1[] = { "A", "GO", "B", "BACK", NULL };
      footer(f, m, d2, b, two ? hk2 : hk1);
      break; }
    case S_OPTIONS: {                                   /* the hub: one big row per category */
      int pw = f->w - 24 < 340 ? f->w - 24 : 340, px0 = (f->w - pw) / 2, top = 33, row = 26, ry0 = top + 4;
      panel(f, px0, top, pw, n * row + 8, 200);
      float by = (float)(ry0 + sel * row);
      if (m->snap) m->bar_y = by; else ease(&m->bar_y, by, k);
      sel_bar(f, px0 + 4, (int)(m->bar_y + 0.5f), pw - 8, row - 2);
      for (int i = 0; i < n; i++) {
        int y = ry0 + i * row, s = i == sel;
        if (s) faceo(f, px0 + 9, y + 7 + (int)(sinf(t * 4.0f) * 1.0f));
        txt(f, px0 + 26, y + 5, it[i].label, s ? C_SEL : C_ITEM, 2);
        if (it[i].sub[0]) txtr(f, px0 + pw - 14, y + 9, it[i].sub, s ? C_ITEM : C_DIM, 1);
        else txtr(f, px0 + pw - 14, y + 9, ">", s ? C_SEL : C_DIM, 1);
        add_hit(m, px0, y, pw, row, i, 0);
      }
      s_ox = 0; s_a = 256;
      menu_header(f, "OPTIONS", t);
      footer(f, m, desc, NULL, HK_OPEN);
      break; }
    case S_GAMEPLAY: case S_AUDIO: case S_DREAMOPT: case S_GRAPHICS: case S_CONTROLS: {
      draw_page(f, m, it, n, sel, k);
      s_ox = 0; s_a = 256;
      const char *title = m->screen == S_GAMEPLAY ? "GAMEPLAY" : m->screen == S_AUDIO ? "AUDIO" : m->screen == S_DREAMOPT ? "DREAMS" :
                          m->screen == S_GRAPHICS ? "GRAPHICS" : "CONTROLS";
      menu_header(f, title, t);
      int bind = n > 0 && it[sel].id >= I_BIND0 && it[sel].id < I_BIND0 + ACT_N;
      static const char *const hk_bind[] = { "A", "REBIND", "B", "BACK", NULL };
      static const char *const hk_act[] = { "A", "RESET", "B", "BACK", NULL };
      footer(f, m, desc, NULL, bind ? hk_bind : n > 0 && it[sel].id == I_RESETBIND ? hk_act : HK_PAGE);
      if (m->capture) {                                  /* the "press a button" dialog */
        int dw = f->w - 60 < 260 ? f->w - 60 : 260, dx = (f->w - dw) / 2;
        panel(f, dx, 78, dw, 74, 245);
        char b2[64]; snprintf(b2, sizeof b2, "PRESS A BUTTON FOR %s", input_action_name(m->capture - 1));
        text_c(f, f->w / 2, 90, b2, C_HEAD, 1);
        snprintf(b2, sizeof b2, "ANY CONTROLLER (%d)", (int)ceilf(m->cap_t)); text_c(f, f->w / 2, 108, b2, C_SEL, 1);
        int pl = (int)((dw - 40) * (m->cap_t / 5.0f)); ui_rect(f, dx + 20, 122, dw - 40, 2, 0xFF2A2250u, 255); ui_rect(f, dx + 20, 122, pl, 2, C_SEL, 255);
        text_c(f, f->w / 2, 134, "WAIT TO CANCEL", C_DIM, 1);
      }
      break; }
    case S_CONFIRM_NEW: {
      int pw = f->w - 40 < 280 ? f->w - 40 : 280, px0 = (f->w - pw) / 2;
      panel(f, px0, 46, pw, 132, 210);
      txtc(f, f->w / 2, 60, "START A NEW DREAM JOURNAL?", C_HEAD, 1);
      snprintf(b, sizeof b, "DAY %d AND THE DREAM CHART", g_save.day); txtc(f, f->w / 2, 76, b, C_ITEM, 1);
      txtc(f, f->w / 2, 87, "WILL BE LOST.", C_ITEM, 1);
      draw_grow(f, m, it, n, sel, f->w / 2, 112, 16, 22, t, k);
      s_ox = 0; s_a = 256;
      menu_header(f, "NEW GAME", t);
      static const char *const hk[] = { "A", "SELECT", "B", "BACK", NULL };
      footer(f, m, desc, NULL, hk);
      break; }
  }
  s_ox = 0; s_a = 256;
  m->snap = 0;
}

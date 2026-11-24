/* splash.c - the boot splash "PORTED BY CUPPAX" (see menu.h).
 * A retro boot: CRT power-on line, warp starfield + perspective grid, the LSD ring (TITLE.TIM) spinning in, the credit text
 * scrambling in letter by letter (bouncing, rainbow gradient, glow, echo trail, a highlight sweep), static bursts and glitch
 * slices, scanlines + RGB split, then a whiteout into the title. Procedural, drawn into the 320x240 menu frame. */
#include "menu.h"
#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t font57[64][7];               /* menu.c */
extern uint32_t *title_px; extern int title_w, title_h;
uint32_t menu_hash(uint32_t x);
#define hash menu_hash
#define KEY_R 24
#define KEY_G 8
#define KEY_B 65
#define CELL 74
#define SPLASH_LEN 4.6f

float splash_length(void) { return SPLASH_LEN; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease_out(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }
static float ease_back(float t) { t = clampf(t, 0, 1); const float c1 = 1.9f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
static uint32_t rgb(float r, float g, float b) {
  int R = (int)(clampf(r, 0, 1) * 255), G = (int)(clampf(g, 0, 1) * 255), B = (int)(clampf(b, 0, 1) * 255);
  return 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}
static void hsv(float h, float s, float v, float *r, float *g, float *b) {
  h = h - floorf(h); float i = floorf(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch ((int)i % 6) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q;
  }
}
static void add_px(Frame *f, int x, int y, float r, float g, float b) {
  if ((unsigned)x >= (unsigned)f->w || (unsigned)y >= (unsigned)f->h) return;
  uint32_t d = f->color[y * f->w + x];
  int R = (int)(d >> 16 & 255) + (int)(r * 255), G = (int)(d >> 8 & 255) + (int)(g * 255), B = (int)(d & 255) + (int)(b * 255);
  f->color[y * f->w + x] = 0xFF000000u | ((uint32_t)(R > 255 ? 255 : R) << 16) | ((uint32_t)(G > 255 ? 255 : G) << 8) | (uint32_t)(B > 255 ? 255 : B);
}
static void line_add(Frame *f, float x0, float y0, float x1, float y1, float r, float g, float b) {
  int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0))) + 1; if (n > 800) n = 800;
  for (int i = 0; i <= n; i++) { float t = (float)i / n; add_px(f, (int)(x0 + (x1 - x0) * t), (int)(y0 + (y1 - y0) * t), r, g, b); }
}

static uint8_t glow_a[MENU_W * MENU_H], glow_b[MENU_W * MENU_H];
static void glow_blur(int w, int h) {            /* two box passes (radius 3) over glow_a */
  for (int pass = 0; pass < 2; pass++) {
    for (int y = 0; y < h; y++) {
      int acc = 0; const uint8_t *s = glow_a + y * w; uint8_t *d = glow_b + y * w;
      for (int x = -3; x < w + 3; x++) { if (x + 3 < w) acc += s[x + 3]; if (x - 4 >= 0) acc -= s[x - 4]; if (x >= 0 && x < w) d[x] = (uint8_t)(acc / 7); }
    }
    for (int x = 0; x < w; x++) {
      int acc = 0;
      for (int y = -3; y < h + 3; y++) { if (y + 3 < h) acc += glow_b[(y + 3) * w + x]; if (y - 4 >= 0) acc -= glow_b[(y - 4) * w + x]; if (y >= 0 && y < h) glow_a[y * w + x] = (uint8_t)(acc / 7); }
    }
  }
}

/* one glyph of the menu font as smoothly scaled bold blocks (px = block size, may be fractional).
 * mode 0: dark outline, 1: fill (rainbow gradient, top highlight, white sweep at x = sweep), 2: glow mask */
static void big_glyph(Frame *f, int ch, float cx, float cy, float px, float hue, float bright, int mode, float sweep) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch < 32 || ch > 95 || px < 0.2f) return;
  const uint8_t *g = font57[ch - 32];
  float x0 = cx - 3.0f * px, y0 = cy - 3.5f * px;
  for (int r = 0; r < 7; r++) for (int k = 0; k < 5; k++) {
    if (!(g[r] & (16 >> k))) continue;
    float bx0 = x0 + k * px, by0 = y0 + r * px, bx1 = bx0 + px * 1.5f, by1 = by0 + px;
    int ix0 = (int)floorf(bx0), iy0 = (int)floorf(by0), ix1 = (int)ceilf(bx1), iy1 = (int)ceilf(by1);
    if (mode == 0) { ix0 -= 1; iy0 -= 1; ix1 += 1; iy1 += 2; }
    for (int y = iy0; y < iy1; y++) for (int x = ix0; x < ix1; x++) {
      if ((unsigned)x >= (unsigned)f->w || (unsigned)y >= (unsigned)f->h) continue;
      if (mode == 0) { f->color[y * f->w + x] = 0xFF0A0418u; continue; }
      if (mode == 2) { glow_a[y * f->w + x] = 255; continue; }
      float v = clampf((y - y0) / (7 * px), 0, 1);                 /* 0 top .. 1 bottom */
      float rr, gg, bb; hsv(hue + v * 0.18f + x * 0.002f, v < 0.15f ? 0.4f : 0.75f, 1.0f, &rr, &gg, &bb);
      float shade = (1.05f - 0.45f * v) * bright;
      float sw = expf(-(x - sweep) * (x - sweep) / 120.0f) * bright;   /* white highlight sweep */
      f->color[y * f->w + x] = rgb(rr * shade + sw, gg * shade + sw, bb * shade + sw);
    }
  }
}

static const char scr_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*+=?@";

/* a line of credit text: letters scramble, then lock and bounce in one after another, with a slow wave */
static void credit_text(Frame *f, const char *s, float cx, float cy, float px, float t0, float step, float t, float hue0, int pass, float sweep, float alpha) {
  int n = (int)strlen(s); float adv = 6.6f * px, x = cx - (n - 1) * adv * 0.5f;
  for (int i = 0; i < n; i++, x += adv) {
    if (s[i] == ' ') continue;
    float lt = t - (t0 + i * step); if (lt < 0) continue;
    int ch = s[i], scr = lt < 0.22f;
    if (scr) ch = scr_chars[hash((uint32_t)(i * 131 + (int)(t * 30))) % (sizeof scr_chars - 1)];   /* scramble before it locks */
    float sc = scr ? 0.7f : ease_back((lt - 0.22f) / 0.35f);
    float bounce = scr ? 0 : -sinf(clampf((lt - 0.22f) / 0.35f, 0, 1) * 3.14159f) * px * 2.5f;
    float wave = sinf(t * 3.2f + i * 0.7f) * px * 0.35f;
    big_glyph(f, ch, x, cy + bounce + wave, px * sc, hue0 + i * 0.07f + t * 0.12f, alpha * (scr ? 0.6f : 1.0f), pass, sweep);
  }
}

static void rotozoom_ring(Frame *f, float cx, float cy, float scale, float ang, float a) {
  if (!title_px || scale < 0.02f || a <= 0) return;
  float r = CELL * 0.5f * scale * 1.42f, c = cosf(ang), s = sinf(ang);
  int x0 = (int)(cx - r), x1 = (int)(cx + r), y0 = (int)(cy - r), y1 = (int)(cy + r);
  int sx0 = 12 + CELL, sy0 = 8 + CELL;                            /* cell (1,1): the big LSD logo ring */
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
    if ((unsigned)x >= (unsigned)f->w || (unsigned)y >= (unsigned)f->h) continue;
    float dx = (x - cx) / scale, dy = (y - cy) / scale;
    int u = (int)(dx * c + dy * s + CELL / 2), v = (int)(-dx * s + dy * c + CELL / 2);
    if ((unsigned)u >= CELL || (unsigned)v >= CELL || sy0 + v >= title_h) continue;
    uint32_t p = title_px[(sy0 + v) * title_w + sx0 + u];
    float dr = fmaxf(0, ((int)(p >> 16 & 255) - KEY_R) / 255.0f), dg = fmaxf(0, ((int)(p >> 8 & 255) - KEY_G) / 255.0f), db = fmaxf(0, ((int)(p & 255) - KEY_B) / 255.0f);
    add_px(f, x, y, dr * a, dg * a, db * a);
  }
}

static uint8_t vign[MENU_W * MENU_H]; static int vign_ok;

void splash_draw(Frame *f, float t) {
  int W = f->w, H = f->h;
  if (W != MENU_W || H != MENU_H) return;
  if (!vign_ok) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float dx = (x - W / 2) / (float)W, dy = (y - H / 2) / (float)H; vign[y * W + x] = (uint8_t)(clampf(1.0f - (dx * dx + dy * dy) * 1.3f, 0, 1) * 255); }
    vign_ok = 1;
  }
  /* ---- background: deep night, glowing horizon ---- */
  float scene = ease_out((t - 0.3f) / 0.6f);
  const int hz = 142;
  for (int y = 0; y < H; y++) {
    float k = y < hz ? (float)y / hz : 1.0f - (float)(y - hz) / (H - hz) * 0.6f, glow = expf(-fabsf((float)(y - hz)) / 10.0f);
    uint32_t c = rgb(scene * (0.03f + 0.10f * k + 0.55f * glow), scene * (0.01f + 0.02f * k + 0.12f * glow), scene * (0.08f + 0.16f * k + 0.45f * glow));
    for (int x = 0; x < W; x++) f->color[y * W + x] = c;
  }
  if (scene > 0) {
    for (int i = 0; i < 160; i++) {                               /* warp stars streaking out of the vanishing point */
      uint32_t h = hash((uint32_t)i * 2654435761u + 7u);
      float sx = ((h & 1023) / 1023.0f - 0.5f) * 2.0f, sy = (((h >> 10) & 1023) / 1023.0f - 0.5f) * 1.2f;
      float speed = 0.35f + ((h >> 20) & 255) / 255.0f * 0.5f;
      float z = 1.0f - fmodf(t * speed + ((h >> 4) & 1023) / 1023.0f, 1.0f); z = z * 0.95f + 0.05f;
      float z2 = fminf(z + 0.04f + 0.05f * speed, 1.0f);
      float x0 = 160 + sx * 60 / z, y0 = hz - 40 + sy * 60 / z, x1 = 160 + sx * 60 / z2, y1 = hz - 40 + sy * 60 / z2;
      if (y0 > hz || y1 > hz) continue;
      float b = scene * (1.0f - z) * 0.9f;
      line_add(f, x1, y1, x0, y0, b * 0.7f, b * 0.8f, b);
    }
    float scroll = fmodf(t * 1.6f, 1.0f);                         /* perspective grid floor rolling towards us */
    for (int i = 0; i < 14; i++) {
      float z = (i + 1 - scroll) * 0.6f, y = hz + 60.0f / z;
      if (y >= H || y <= hz) continue;
      float b = scene * clampf(1.2f - z * 0.12f, 0, 1);
      line_add(f, 0, y, (float)(W - 1), y, b * 0.9f, b * 0.15f, b * 0.8f);
    }
    for (int i = -12; i <= 12; i++) line_add(f, 160 + i * 5.0f, (float)hz + 1, 160 + i * 56.0f, (float)(H - 1), scene * 0.55f, scene * 0.1f, scene * 0.7f);
  }
  /* ---- the LSD ring spins and zooms in, then floats ---- */
  float rt = (t - 0.55f) / 0.9f;
  if (rt > 0) rotozoom_ring(f, 160, 64 + sinf(t * 2.0f) * 2.0f, ease_back(rt) * 1.05f, (1.0f - ease_out(rt)) * 9.4f, clampf(rt * 2, 0, 1) * clampf((4.2f - t) / 0.5f, 0, 1));
  /* ---- credit text: glow, echo trail, outline, fill ---- */
  float sweep = -60 + (t - 2.55f) * 520.0f, ta = 1.0f;          /* the whiteout takes the text away */
  memset(glow_a, 0, sizeof glow_a);
  credit_text(f, "PORTED BY", 160, 126, 2.0f, 1.25f, 0.045f, t, 0.55f, 2, -999, 1);
  credit_text(f, "CUPPAX", 160, 168, 4.6f, 1.65f, 0.13f, t, 0.85f, 2, -999, 1);
  glow_blur(W, H);
  float gp = (0.55f + 0.2f * sinf(t * 5.0f)) * ta;
  for (int i = 0; i < W * H; i++) if (glow_a[i]) {
    float g = glow_a[i] / 255.0f * gp, rr, gg, bb; hsv(0.85f + (i % W) * 0.002f + t * 0.1f, 0.7f, 1, &rr, &gg, &bb);
    add_px(f, i % W, i / W, rr * g * 0.8f, gg * g * 0.6f, bb * g);
  }
  for (int echo = 2; echo >= 1; echo--) {                         /* echo trail: the same text a moment ago, fainter */
    float te = t - echo * 0.07f; if (te < 1.65f) continue;
    credit_text(f, "CUPPAX", 160 + echo * 2.0f, 168 + echo * 2.0f, 4.6f, 1.65f, 0.13f, te, 0.85f + 0.3f * echo, 1, -999, 0.3f / echo * ta);
  }
  credit_text(f, "PORTED BY", 160, 126, 2.0f, 1.25f, 0.045f, t, 0.55f, 0, -999, 1);
  credit_text(f, "PORTED BY", 160, 126, 2.0f, 1.25f, 0.045f, t, 0.55f, 1, sweep, ta);
  credit_text(f, "CUPPAX", 160, 168, 4.6f, 1.65f, 0.13f, t, 0.85f, 0, -999, 1);
  credit_text(f, "CUPPAX", 160, 168, 4.6f, 1.65f, 0.13f, t, 0.85f, 1, sweep, ta);

  /* ---- static bursts with glitch slices ---- */
  static const float bursts[][2] = { { 0.42f, 0.16f }, { 2.20f, 0.09f }, { 3.55f, 0.12f } };
  float noise = 0;
  for (int i = 0; i < 3; i++) { float d = t - bursts[i][0]; if (d >= 0 && d < bursts[i][1]) noise = fmaxf(noise, 1.0f - d / bursts[i][1]); }
  if (noise > 0) {
    uint32_t seed = (uint32_t)(t * 997.0f);
    for (int k = 0; k < 6; k++) {                                  /* horizontal slices shoved sideways */
      uint32_t h = hash(seed * 31u + (uint32_t)k); int y0 = (int)(h % (unsigned)H), hh = 2 + (int)((h >> 8) % 10u), off = (int)((h >> 16) % 25u) - 12;
      for (int y = y0; y < y0 + hh && y < H; y++) {
        uint32_t row[MENU_W]; memcpy(row, f->color + y * W, sizeof row);
        for (int x = 0; x < W; x++) { int sx = x - off; f->color[y * W + x] = row[sx < 0 ? 0 : sx >= W ? W - 1 : sx]; }
      }
    }
    int amt = (int)(noise * 150);
    for (int i = 0; i < W * H; i++) { uint32_t h = hash(seed + (uint32_t)i * 2246822519u); if ((int)(h & 255) < amt) { uint32_t v = (h >> 8) & 255; f->color[i] = 0xFF000000u | (v << 16) | (v << 8) | v; } }
  }
  /* ---- CRT power-on: a bright line opens into the picture ---- */
  if (t < 0.5f) {
    float hw = ease_out(t / 0.18f) * W * 0.5f, hh = t < 0.18f ? 0.5f : ease_out((t - 0.18f) / 0.3f) * H * 0.5f;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      float dx = fabsf(x - W * 0.5f), dy = fabsf(y - H * 0.5f);
      uint32_t *p = &f->color[y * W + x];
      if (dx > hw || dy > hh + 1) { *p = 0xFF000000u; continue; }
      float flash = t < 0.18f ? 1.0f - dx / (W * 0.5f + 1) * 0.5f : clampf(1.0f - (t - 0.18f) / 0.3f, 0, 1) * (dy < hh * 0.25f + 1.5f ? 1.0f : 0.5f);
      int r = (int)(*p >> 16 & 255) + (int)(flash * 230), g = (int)(*p >> 8 & 255) + (int)(flash * 240), b = (int)(*p & 255) + (int)(flash * 255);
      *p = 0xFF000000u | ((uint32_t)(r > 255 ? 255 : r) << 16) | ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b);
    }
  }
  /* ---- CRT post: RGB split, scanlines, vignette, power-on flicker, and the whiteout into the title ---- */
  float wo = clampf((t - 3.95f) / (SPLASH_LEN - 3.95f), 0, 1);
  int split = 1 + (int)(noise * 4) + (t < 0.9f ? 1 : 0);
  float flick = t < 0.9f ? 0.92f + 0.08f * (float)(hash((uint32_t)(t * 60)) & 255) / 255.0f : 1.0f;
  for (int y = 0; y < H; y++) {
    uint32_t row[MENU_W]; memcpy(row, f->color + y * W, sizeof row);
    int scan = (y & 1) ? 200 : 256;
    for (int x = 0; x < W; x++) {
      int xr = x + split < W ? x + split : W - 1, xb = x - split >= 0 ? x - split : 0;
      int r = (int)(row[xr] >> 16 & 255), g = (int)(row[x] >> 8 & 255), b = (int)(row[xb] & 255);
      int m = (int)(scan * vign[y * W + x] / 255 * flick);
      r = r * m >> 8; g = g * m >> 8; b = b * m >> 8;
      if (wo > 0) { int a = (int)(wo * 256); r = (r * (256 - a) + 255 * a) >> 8; g = (g * (256 - a) + 255 * a) >> 8; b = (b * (256 - a) + 255 * a) >> 8; }
      f->color[y * W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
  }
}

/* a short, quiet synthesized boot chime (rising C-E-G-C arpeggio, then a high bell) through the music path */
void splash_chime(void) {
  const int rate = 22050, n = rate * 7;                           /* a long silent tail, so the music loop never comes round */
  size_t bytes = 44 + (size_t)n * 2;
  uint8_t *w = (uint8_t *)calloc(1, bytes); if (!w) return;
  uint32_t v = (uint32_t)bytes - 8;
  memcpy(w, "RIFF", 4); for (int k = 0; k < 4; k++) w[4 + k] = (uint8_t)(v >> (8 * k));
  memcpy(w + 8, "WAVEfmt ", 8); w[16] = 16; w[20] = 1; w[22] = 1;
  for (int k = 0; k < 4; k++) { w[24 + k] = (uint8_t)((uint32_t)rate >> (8 * k)); w[28 + k] = (uint8_t)((uint32_t)(rate * 2) >> (8 * k)); }
  w[32] = 2; w[34] = 16; memcpy(w + 36, "data", 4); v = (uint32_t)n * 2; for (int k = 0; k < 4; k++) w[40 + k] = (uint8_t)(v >> (8 * k));
  /* cheap synthesis (this runs on a 1.2 GHz PPC at boot): a sine table, phase accumulators, multiplicative envelopes */
  static int16_t sine[1024]; for (int i = 0; i < 1024; i++) sine[i] = (int16_t)(sinf(i * 6.2831853f / 1024) * 32767);
  static const float notes[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f };
  static const float at[] = { 0.45f, 0.62f, 0.79f, 0.96f, 1.70f };
  for (int k = 0; k < 5; k++) {
    uint32_t ph = 0, ph3 = 0, inc = (uint32_t)(notes[k] / rate * 4294967296.0), inc3 = (uint32_t)(notes[k] * 3.01f / rate * 4294967296.0);
    float env = 0, att = 1.0f - expf(-200.0f / rate), dec = expf(-(k == 4 ? 1.6f : 3.0f) / rate), amp = 1;
    int i0 = (int)(at[k] * rate), i1 = i0 + rate * 3; if (i1 > n) i1 = n;
    for (int i = i0; i < i1; i++) {
      env += (1.0f - env) * att; amp *= dec; ph += inc; ph3 += inc3;
      int a = sine[ph >> 22], b = sine[(ph * 2u) >> 22], c = sine[ph3 >> 22];
      float smp = (a + 0.3f * b + 0.12f * c) * env * amp * 0.16f;
      int q = (int)(int16_t)(w[44 + i * 2] | (w[45 + i * 2] << 8)) + (int)smp;
      q = q < -32767 ? -32767 : q > 32767 ? 32767 : q;
      w[44 + i * 2] = (uint8_t)(q & 255); w[45 + i * 2] = (uint8_t)((q >> 8) & 255);
    }
  }
  audio_music_start(w, bytes); free(w);
}

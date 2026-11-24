#include "raster.h"
#include "lsd.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

void raster_clear(Frame *f, uint32_t argb) {
  int n = f->w * f->h;
  for (int i = 0; i < n; i++) { f->color[i] = argb; f->depth[i] = 0.0f; }
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Texture warping like Revamped's classic shader (LSDR.cginc: clipPos.w += 0.25; w' = lerp(1, w, _AffineIntensity);
 * uv *= w' in the vertex shader, uv / w' per fragment, with hardware perspective-correct varyings). Per pixel that is
 *   u = sum(s_i * q_i * u_i) / sum(s_i * q_i),  q_i = (1 - 0.75 I) / z_i + I     (s_i = screen-space barycentrics)
 * I = 0: perspective-correct, I = 1: nearly affine (PS1). render.c puts q in SVtx. When q hardly varies over a triangle
 * (it is small or far away) the plain affine fixed-point loop is exact enough; otherwise u,v are computed exactly every
 * PSPAN pixels and stepped linearly in between. */
#define PSPAN 8
typedef struct { int on; float P0[3], gx[3], gy[3]; int bias; } Persp;
static inline void persp_setup(Persp *p, const SVtx *t, const SVtx *m, const SVtx *b, float dym, float dyb, float dxm, float dxb, float inv, int bias) {
  float qmin = fminf(t->q, fminf(m->q, b->q)), qmax = fmaxf(t->q, fmaxf(m->q, b->q));
  p->on = qmin > 0 && qmax > qmin * 1.08f;
  if (!p->on) return;
  float A0[3] = { t->u * t->q, t->v * t->q, t->q }, Am[3] = { m->u * m->q, m->v * m->q, m->q }, Ab[3] = { b->u * b->q, b->v * b->q, b->q };
  for (int k = 0; k < 3; k++) {
    float pm = Am[k] - A0[k], pb = Ab[k] - A0[k];
    p->P0[k] = A0[k]; p->gx[k] = (pm * dyb - pb * dym) * inv; p->gy[k] = (pb * dxm - pm * dxb) * inv;
  }
  p->bias = bias;
}
/* exact u,v at the pixel offset (ox, oy) from the top vertex, and the 16.16 steps to reach the exact value n-1 pixels on */
static inline void persp_span(const Persp *p, float ox, float oy, int n, int *iu, int *iv, int *du, int *dv) {
  float q0 = p->P0[2] + p->gx[2] * ox + p->gy[2] * oy; if (q0 < 1e-6f) q0 = 1e-6f;
  float u0 = (p->P0[0] + p->gx[0] * ox + p->gy[0] * oy) / q0, v0 = (p->P0[1] + p->gx[1] * ox + p->gy[1] * oy) / q0;
  *iu = (int)(u0 * 65536.0f) + p->bias; *iv = (int)(v0 * 65536.0f) + p->bias;
  if (n > 1) {
    float e = (float)(n - 1), q1 = q0 + p->gx[2] * e; if (q1 < 1e-6f) q1 = 1e-6f;
    float u1 = (p->P0[0] + p->gx[0] * (ox + e) + p->gy[0] * oy) / q1, v1 = (p->P0[1] + p->gx[1] * (ox + e) + p->gy[1] * oy) / q1;
    *du = (int)((u1 - u0) * 65536.0f / e); *dv = (int)((v1 - v0) * 65536.0f / e);
  } else { *du = *dv = 0; }
}
#define PERSP_STEP(x) \
  if (ps.on && --span <= 0) { int n_ = xe - (x) + 1; if (n_ > PSPAN) n_ = PSPAN; persp_span(&ps, ((x) + 0.5f) - t->x, oy, n_, &iu, &iv, &du, &dv); span = n_; }

/* Scanline rasterizer. Per triangle: plane gradients in float; per row: float setup, then 16.16 fixed-point integer
 * interpolation of u, v, r, g, b and fog distance (cheap on the Espresso), float only for the depth test. */
void raster_tri(Frame *f, const SVtx *a0, const SVtx *b0, const SVtx *c0, const uint32_t *atlas, const WaterTex *wt, const uint8_t *pal, int blend, int ylo, int yhi) {
  const SVtx *t = a0, *m = b0, *b = c0, *tmp;
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  if (m->y > b->y) { tmp = m; m = b; b = tmp; }
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  float area = (m->x - t->x) * (b->y - t->y) - (b->x - t->x) * (m->y - t->y);
  if (area > -0.5f && area < 0.5f) return;
  int y0 = (int)ceilf(t->y - 0.5f), y1 = (int)ceilf(b->y - 0.5f) - 1;
  if (y0 < ylo) y0 = ylo;
  if (y1 > yhi) y1 = yhi;
  if (y0 > y1) return;
  float inv = 1.0f / area;

  float A0[7] = { t->iz, t->u, t->v, t->r, t->g, t->b, t->d };
  float Am[7] = { m->iz, m->u, m->v, m->r, m->g, m->b, m->d };
  float Ab[7] = { b->iz, b->u, b->v, b->r, b->g, b->b, b->d };
  float gx[7], gy[7];
  float dym = m->y - t->y, dyb = b->y - t->y, dxm = m->x - t->x, dxb = b->x - t->x;
  for (int k = 0; k < 7; k++) {
    float pm = Am[k] - A0[k], pb = Ab[k] - A0[k];
    gx[k] = (pm * dyb - pb * dym) * inv;
    gy[k] = (pb * dxm - pm * dxb) * inv;
  }
  float sLong = dyb > 0 ? dxb / dyb : 0, sA = dym > 0 ? dxm / dym : 0, sB = (b->y - m->y) > 0 ? (b->x - m->x) / (b->y - m->y) : 0;
  int fog = f->fog_on; float fscale = f->fog_scale * 65536.0f;
  int du = (int)(gx[1] * 65536.0f), dv = (int)(gx[2] * 65536.0f), dr = (int)(gx[3] * 65536.0f), dg = (int)(gx[4] * 65536.0f), db = (int)(gx[5] * 65536.0f);
  int dd = (int)(gx[6] * fscale);
  int wphase = 0, walpha = 128;
  if (wt) { float tt = f->water_time * wt->speed * (1.0f / 20.0f); wphase = (int)((tt - floorf(tt)) * 65536.0f); }   /* _Time.x = t/20 */
  float diz = gx[0];
  Persp ps; ps.on = 0; if (atlas || wt) persp_setup(&ps, t, m, b, dym, dyb, dxm, dxb, inv, 0x8000);

  for (int y = y0; y <= y1; y++) {
    float yc = y + 0.5f;
    float xa = t->x + sLong * (yc - t->y);
    float xb = yc < m->y ? t->x + sA * (yc - t->y) : m->x + sB * (yc - m->y);
    float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
    int xs = (int)ceilf(xl - 0.5f), xe = (int)ceilf(xr - 0.5f) - 1;
    if (xs < 0) xs = 0;
    if (xe > f->w - 1) xe = f->w - 1;
    if (xs > xe) continue;
    float ox = (xs + 0.5f) - t->x, oy = yc - t->y;
    float iz = A0[0] + gx[0] * ox + gy[0] * oy;
    int iu = (int)((A0[1] + gx[1] * ox + gy[1] * oy) * 65536.0f) + 0x8000;
    int iv = (int)((A0[2] + gx[2] * ox + gy[2] * oy) * 65536.0f) + 0x8000;
    int ir = (int)((A0[3] + gx[3] * ox + gy[3] * oy) * 65536.0f);
    int ig = (int)((A0[4] + gx[4] * ox + gy[4] * oy) * 65536.0f);
    int ib = (int)((A0[5] + gx[5] * ox + gy[5] * oy) * 65536.0f);
    int id = (int)((A0[6] + gx[6] * ox + gy[6] * oy) * fscale);
    uint32_t *crow = f->color + y * f->w;
    float *drow = f->depth + y * f->w;
    int span = 0;
    for (int x = xs; x <= xe; x++, iz += diz, iu += du, iv += dv, ir += dr, ig += dg, ib += db, id += dd) {
      PERSP_STEP(x)
      if (iz <= drow[x]) continue;
      int R, G, B;
      if (wt) {                                              /* water: palette index = (map.r - time) mod 1, alpha = min(_Alpha, map.a) */
        int tx = iu >> 16, ty = iv >> 16;
        if ((unsigned)tx >= (unsigned)wt->mw) tx = tx < 0 ? 0 : wt->mw - 1;
        if ((unsigned)ty >= (unsigned)wt->mh) ty = ty < 0 ? 0 : wt->mh - 1;
        const uint8_t *tp = wt->ra + ((size_t)ty * wt->mw + tx) * 2;
        if (!tp[1]) continue;
        int pidx = ((tp[0] * 257) - wphase) & 0xFFFF;
        const uint8_t *pc = pal + (pidx * wt->pw >> 16) * 3;
        R = pc[0] * clamp255(ir >> 16) >> 7; G = pc[1] * clamp255(ig >> 16) >> 7; B = pc[2] * clamp255(ib >> 16) >> 7;
        if (R > 255) R = 255;
        if (G > 255) G = 255;
        if (B > 255) B = 255;
        int aw = (int)(wt->alpha * 256.0f), ap = tp[1] + (tp[1] >> 7); if (ap < aw) aw = ap;
        walpha = aw;
      } else if (atlas) {
        int tx = iu >> 16, ty = iv >> 16;
        if ((unsigned)tx >= (unsigned)VRAM_W) tx = tx < 0 ? 0 : VRAM_W - 1;
        if ((unsigned)ty >= (unsigned)VRAM_H) ty = ty < 0 ? 0 : VRAM_H - 1;
        uint32_t tc = atlas[ty * VRAM_W + tx];
        if (!(tc >> 24)) continue;                           /* cutout */
        R = (int)((tc >> 16) & 255) * clamp255(ir >> 16) >> 7;
        G = (int)((tc >> 8) & 255) * clamp255(ig >> 16) >> 7;
        B = (int)(tc & 255) * clamp255(ib >> 16) >> 7;
        if (R > 255) R = 255;
        if (G > 255) G = 255;
        if (B > 255) B = 255;
      } else {
        R = clamp255(ir >> 16); G = clamp255(ig >> 16); B = clamp255(ib >> 16);
      }
      if (fog) {
        int fi = id >> 16; if (fi > 255) fi = 255; else if (fi < 0) fi = 0;
        int mm = f->fog_mul[fi];
        R = clamp255((R * mm >> 8) + f->fog_add[fi][0]); G = clamp255((G * mm >> 8) + f->fog_add[fi][1]); B = clamp255((B * mm >> 8) + f->fog_add[fi][2]);
      }
      if (blend) {
        uint32_t d = crow[x]; int ia = wt ? 256 - walpha : 128, aa = wt ? walpha : 128;
        R = (R * aa + (int)(d >> 16 & 255) * ia) >> 8; G = (G * aa + (int)(d >> 8 & 255) * ia) >> 8; B = (B * aa + (int)(d & 255) * ia) >> 8;
      } else drow[x] = iz;
      crow[x] = 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
    }
  }
}

/* The generic texture path of the mesh dreams (RTex): same scanline setup and fixed point as raster_tri, texels wrapped with
 * power-of-two masks (u,v are texels of this texture, any range the 16.16 numbers hold), Unity's colour rule
 * tex * vertex colour * 2 (128 = neutral), cutout at alpha <= 25 (LSDR.cginc 0.1), real alpha blending, water palette cycling */
void raster_tri_tex(Frame *f, const SVtx *a0, const SVtx *b0, const SVtx *c0, const RTex *tx, int va, int ylo, int yhi) {
  const SVtx *t = a0, *m = b0, *b = c0, *tmp;
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  if (m->y > b->y) { tmp = m; m = b; b = tmp; }
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  float area = (m->x - t->x) * (b->y - t->y) - (b->x - t->x) * (m->y - t->y);
  if (area > -0.5f && area < 0.5f) return;
  int y0 = (int)ceilf(t->y - 0.5f), y1 = (int)ceilf(b->y - 0.5f) - 1;
  if (y0 < ylo) y0 = ylo;
  if (y1 > yhi) y1 = yhi;
  if (y0 > y1) return;
  float inv = 1.0f / area;
  float A0[7] = { t->iz, t->u, t->v, t->r, t->g, t->b, t->d };
  float Am[7] = { m->iz, m->u, m->v, m->r, m->g, m->b, m->d };
  float Ab[7] = { b->iz, b->u, b->v, b->r, b->g, b->b, b->d };
  float gx[7], gy[7];
  float dym = m->y - t->y, dyb = b->y - t->y, dxm = m->x - t->x, dxb = b->x - t->x;
  for (int k = 0; k < 7; k++) {
    float pm = Am[k] - A0[k], pb = Ab[k] - A0[k];
    gx[k] = (pm * dyb - pb * dym) * inv;
    gy[k] = (pb * dxm - pm * dxb) * inv;
  }
  float sLong = dyb > 0 ? dxb / dyb : 0, sA = dym > 0 ? dxm / dym : 0, sB = (b->y - m->y) > 0 ? (b->x - m->x) / (b->y - m->y) : 0;
  int fog = f->fog_on; float fscale = f->fog_scale * 65536.0f;
  int du = (int)(gx[1] * 65536.0f), dv = (int)(gx[2] * 65536.0f), dr = (int)(gx[3] * 65536.0f), dg = (int)(gx[4] * 65536.0f), db = (int)(gx[5] * 65536.0f);
  int dd = (int)(gx[6] * fscale);
  float diz = gx[0];
  const uint32_t *px = tx->px; const uint32_t wm = tx->wmask, hm = tx->hmask; const int ws = tx->wshift, mode = tx->mode;
  int wphase = 0;
  if (mode == RT_WATER) { float tt = f->water_time * tx->speed * (1.0f / 20.0f); wphase = (int)((tt - floorf(tt)) * 65536.0f); }
  Persp ps; persp_setup(&ps, t, m, b, dym, dyb, dxm, dxb, inv, 0);
  for (int y = y0; y <= y1; y++) {
    float yc = y + 0.5f;
    float xa = t->x + sLong * (yc - t->y);
    float xb = yc < m->y ? t->x + sA * (yc - t->y) : m->x + sB * (yc - m->y);
    float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
    int xs = (int)ceilf(xl - 0.5f), xe = (int)ceilf(xr - 0.5f) - 1;
    if (xs < 0) xs = 0;
    if (xe > f->w - 1) xe = f->w - 1;
    if (xs > xe) continue;
    float ox = (xs + 0.5f) - t->x, oy = yc - t->y;
    float iz = A0[0] + gx[0] * ox + gy[0] * oy;
    int iu = (int)floorf((A0[1] + gx[1] * ox + gy[1] * oy) * 65536.0f);
    int iv = (int)floorf((A0[2] + gx[2] * ox + gy[2] * oy) * 65536.0f);
    int ir = (int)((A0[3] + gx[3] * ox + gy[3] * oy) * 65536.0f);
    int ig = (int)((A0[4] + gx[4] * ox + gy[4] * oy) * 65536.0f);
    int ib = (int)((A0[5] + gx[5] * ox + gy[5] * oy) * 65536.0f);
    int id = (int)((A0[6] + gx[6] * ox + gy[6] * oy) * fscale);
    uint32_t *crow = f->color + y * f->w;
    float *drow = f->depth + y * f->w;
    int span = 0;
    for (int x = xs; x <= xe; x++, iz += diz, iu += du, iv += dv, ir += dr, ig += dg, ib += db, id += dd) {
      PERSP_STEP(x)
      if (iz <= drow[x]) continue;
      uint32_t tc = px[(((uint32_t)(iv >> 16) & hm) << ws) + ((uint32_t)(iu >> 16) & wm)];
      int A = (int)(tc >> 24), R, G, B;
      if (mode == RT_CUTOUT) {
        if (A <= 25) continue;
        R = (int)((tc >> 16) & 255) * clamp255(ir >> 16) >> 7;
        G = (int)((tc >> 8) & 255) * clamp255(ig >> 16) >> 7;
        B = (int)(tc & 255) * clamp255(ib >> 16) >> 7;
      } else if (mode == RT_WATER) {
        if (!A) continue;
        int pidx = ((int)((tc >> 16) & 255) * 257 - wphase) & 0xFFFF;
        uint32_t pc = tx->pal[pidx * tx->palw >> 16];
        R = (int)((pc >> 16) & 255) * clamp255(ir >> 16) >> 7;
        G = (int)((pc >> 8) & 255) * clamp255(ig >> 16) >> 7;
        B = (int)(pc & 255) * clamp255(ib >> 16) >> 7;
        int ap = A + (A >> 7); A = ap < tx->walpha ? ap : tx->walpha;
      } else {
        A = (A + (A >> 7)) * va >> 8;
        if (A <= 0) continue;
        R = (int)((tc >> 16) & 255) * clamp255(ir >> 16) >> 7;
        G = (int)((tc >> 8) & 255) * clamp255(ig >> 16) >> 7;
        B = (int)(tc & 255) * clamp255(ib >> 16) >> 7;
      }
      if (R > 255) R = 255;
      if (G > 255) G = 255;
      if (B > 255) B = 255;
      if (fog) {
        int fi = id >> 16; if (fi > 255) fi = 255; else if (fi < 0) fi = 0;
        int mm = f->fog_mul[fi];
        R = clamp255((R * mm >> 8) + f->fog_add[fi][0]); G = clamp255((G * mm >> 8) + f->fog_add[fi][1]); B = clamp255((B * mm >> 8) + f->fog_add[fi][2]);
      }
      if (mode == RT_CUTOUT) drow[x] = iz;
      else {                                                     /* blended: alpha 0..256 */
        uint32_t d = crow[x]; int ia = 256 - A;
        R = (R * A + (int)(d >> 16 & 255) * ia) >> 8; G = (G * A + (int)(d >> 8 & 255) * ia) >> 8; B = (B * A + (int)(d & 255) * ia) >> 8;
      }
      crow[x] = 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
    }
  }
}

/* ---------------------------------------------------------------- lists, bands, threads */

static RTri *list_add(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, int blend) {
  float mn = a->y, mx = a->y;
  if (b->y < mn) mn = b->y; if (c->y < mn) mn = c->y;
  if (b->y > mx) mx = b->y; if (c->y > mx) mx = c->y;
  int ymin = (int)floorf(mn), ymax = (int)ceilf(mx);
  if (ymax < 0 || ymin >= f->h) return NULL;
  RList *l = &f->lists[blend ? 1 : 0];
  if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4096; l->t = (RTri *)realloc(l->t, (size_t)l->cap * sizeof(RTri)); }
  RTri *t = &l->t[l->n++];
  t->v[0] = *a; t->v[1] = *b; t->v[2] = *c; t->ymin = ymin; t->ymax = ymax;
  return t;
}

void raster_submit(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const uint32_t *atlas, const WaterTex *wt, const uint8_t *pal, int blend) {
  RTri *t = list_add(f, a, b, c, blend); if (!t) return;
  t->atlas = atlas; t->wt = wt; t->pal = pal; t->tex = NULL; t->va = 256;
}

void raster_submit_tex(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const RTex *tex, int va) {
  RTri *t = list_add(f, a, b, c, tex->mode != RT_CUTOUT); if (!t) return;
  t->atlas = NULL; t->wt = NULL; t->pal = NULL; t->tex = tex; t->va = va;
}

#define BAND_H 16
#define MAX_WORKERS 4
static int nthreads = 1;
static SDL_Thread *workers[MAX_WORKERS]; static SDL_sem *start_sem[MAX_WORKERS], *done_sem;
static SDL_atomic_t next_band; static volatile int quit_workers;
static Frame *job_f; static BandPre job_pre; static void *job_ud;

static void do_bands(void) {
  Frame *f = job_f; int nbands = (f->h + BAND_H - 1) / BAND_H;
  for (;;) {
    int b = SDL_AtomicAdd(&next_band, 1);
    if (b >= nbands) break;
    int y0 = b * BAND_H, y1 = y0 + BAND_H - 1; if (y1 > f->h - 1) y1 = f->h - 1;
    if (job_pre) job_pre(f, y0, y1, job_ud);
    for (int li = 0; li < 2; li++) {
      const RList *l = &f->lists[li];
      for (int i = 0; i < l->n; i++) {
        const RTri *t = &l->t[i];
        if (t->ymax < y0 || t->ymin > y1) continue;
        if (t->tex) raster_tri_tex(f, &t->v[0], &t->v[1], &t->v[2], t->tex, t->va, y0, y1);
        else raster_tri(f, &t->v[0], &t->v[1], &t->v[2], t->atlas, t->wt, t->pal, li, y0, y1);
      }
    }
  }
}

static void (*par_fn)(int slot, void *ud); static void *par_ud;   /* raster_parallel job (NULL: bands) */
static int nactive = 1;                         /* threads taking part (<= nthreads), raster_threads_set */
static int worker(void *p) {
  int id = (int)(intptr_t)p;
  for (;;) {
    SDL_SemWait(start_sem[id]);
    if (quit_workers) return 0;
    if (par_fn) par_fn(id + 1, par_ud); else do_bands();
    SDL_SemPost(done_sem);
  }
}

void raster_parallel(void (*fn)(int slot, void *ud), void *ud) {
  int na = nactive;
  if (na <= 1) { fn(0, ud); return; }
  par_fn = fn; par_ud = ud;
  for (int i = 0; i < na - 1; i++) SDL_SemPost(start_sem[i]);
  fn(0, ud);
  for (int i = 0; i < na - 1; i++) SDL_SemWait(done_sem);
  par_fn = NULL;
}

void raster_threads_set(int n) { if (n < 1) n = 1; if (n > nthreads) n = nthreads; nactive = n; }
void raster_threads_init(int n) {
  if (n < 1) n = 1;
  if (n > MAX_WORKERS + 1) n = MAX_WORKERS + 1;
  if (nthreads > 1) return;                     /* already started */
  nthreads = n;
  done_sem = SDL_CreateSemaphore(0);
  for (int i = 0; i < n - 1; i++) {
    start_sem[i] = SDL_CreateSemaphore(0);
    workers[i] = SDL_CreateThread(worker, "raster", (void *)(intptr_t)i);
    if (!workers[i]) { nthreads = i + 1; break; }
  }
  nactive = nthreads;
}
int raster_threads(void) { return nactive; }

void raster_render(Frame *f, BandPre pre, void *ud) {
  job_f = f; job_pre = pre; job_ud = ud;
  SDL_AtomicSet(&next_band, 0);
  int na = nactive;                              /* idle workers just stay parked on their semaphore */
  for (int i = 0; i < na - 1; i++) SDL_SemPost(start_sem[i]);
  do_bands();
  for (int i = 0; i < na - 1; i++) SDL_SemWait(done_sem);
  f->lists[0].n = f->lists[1].n = 0;
}

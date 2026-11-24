/* raster.h - tiny PS1-style software triangle rasterizer (affine UVs, Gouraud colour, z-buffer, cutout + 50% blend) */
#ifndef RASTER_H
#define RASTER_H
#include <stdint.h>
#include "lsd.h"

/* floor/ceil for |v| < 2^31 without the libm call (the Espresso has no rounding instruction for them: one convert instead) */
static inline int ifloorf(float v) { int i = (int)v; return i - (v < (float)i); }
static inline int iceilf(float v) { int i = (int)v; return i + (v > (float)i); }
/* fminf/fmaxf/sqrtf are library calls on the 750 (NaN rules; no fsqrt instruction): the hot paths use these instead */
static inline float fmn(float a, float b) { return a < b ? a : b; }
static inline float fmx(float a, float b) { return a > b ? a : b; }
static inline float fsqrt(float x) {                /* frsqrte estimate + 3 Newton steps: float accuracy */
  if (!(x > 0.0f)) return 0.0f;
#if defined(__powerpc__) || defined(__PPC__)
  double e, xd = x; __asm__("frsqrte %0,%1" : "=f"(e) : "f"(xd)); float y = (float)e;
#else
  float y = 1.0f / __builtin_sqrtf(x);
#endif
  float hx = 0.5f * x;
  y = y * (1.5f - hx * y * y); y = y * (1.5f - hx * y * y); y = y * (1.5f - hx * y * y);
  return x * y;
}
typedef struct { float x, y, iz, u, v, r, g, b, d, q; } SVtx;   /* screen x,y in pixels; iz = 1/z; u,v atlas texels; r,g,b 0..255 (128 = neutral); d = distance from the camera (fog); q = texture warp weight (raster.c, Revamped's _AffineIntensity) */
/* a texture of its own (mesh dreams): w x h ARGB8888, both powers of two, wrapped (repeat), nearest. u,v of the SVtx are then
 * texels of this texture. mode: 0 cutout (alpha <= 25 discarded, opaque), 1 alpha blend (src*a + dst*(1-a), a = texel alpha *
 * the triangle's vertex alpha), 2 water (texel red = palette position cycling with time, alpha min(texel a, walpha)) */
typedef struct {
  const uint32_t *px; int w, h, wshift; uint32_t wmask, hmask;
  int mode;
  const uint32_t *pal; int palw; int walpha; float speed;   /* water: palette (palw x 1), _Alpha * 256, _AnimationSpeed */
} RTex;
enum { RT_CUTOUT, RT_BLEND, RT_WATER };
typedef struct { SVtx v[3]; const uint32_t *atlas; const WaterTex *wt; const uint8_t *pal; int ymin, ymax; const RTex *tex; int va; } RTri;   /* a projected triangle waiting to be drawn; tex != NULL: generic texture path (va = vertex alpha 0..256) */
/* a triangle list with per-band bins: list_add files each triangle's index under every 16-row band it touches, so a band
 * only walks its own triangles (far views put 50k+ tiny triangles in the lists) */
#define RBAND_H 16
#define RBANDS_MAX 32                 /* 480 lines / 16 = 30 */
#define RLIST_MAX (1 << 20)           /* hard cap per list: triangles past it are dropped instead of growing without end */
typedef struct { RTri *t; int n, cap; int *bin[RBANDS_MAX]; int bn[RBANDS_MAX], bc[RBANDS_MAX]; } RList;
void rlist_clear(RList *l);

#define RATT_MAX 8
typedef struct {
  uint32_t *color; float *depth; int w, h;
  RList lists[2];                 /* [0] opaque, [1] 50% blended (drawn after) */
  /* lists filled on other threads (render.c's projection slots), drawn BEFORE lists[] of the same kind; raster_render
   * detaches them (their owner clears them before the next fill) */
  RList *att[RATT_MAX][2]; int natt;
  /* classic LSD fog (LSDRFog.cginc): per-pixel distance -> lookup; out = colour * fog_mul/256 + fog_add. NULL fog_mul = off */
  float water_time;               /* seconds, for the palette cycling of water */
  uint16_t fog_mul[256]; uint8_t fog_add[256][3]; float fog_scale; int fog_on;
} Frame;

void raster_clear(Frame *f, uint32_t argb);
/* atlas == NULL draws the vertex colour directly. blend: 50% mix with what is already there, no depth write */
void raster_tri(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const uint32_t *atlas, const WaterTex *wt, const uint8_t *pal, int blend, int ylo, int yhi);

/* Frame drawing is split into horizontal bands of 16 rows that worker threads grab dynamically.
 * 1) the main thread projects triangles into f->lists with raster_submit; 2) raster_render clears each band through `pre`
 * (sky) and then draws both lists restricted to that band. nthreads includes the calling thread. */
typedef void (*BandPre)(Frame *f, int y0, int y1, void *ud);
void raster_submit(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const uint32_t *atlas, const WaterTex *wt, const uint8_t *pal, int blend);
/* the generic texture path (RTex); blend and water triangles go to the blended list automatically */
void raster_submit_tex(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const RTex *tex, int va);
void raster_tri_tex(Frame *f, const SVtx *a, const SVtx *b, const SVtx *c, const RTex *tex, int va, int ylo, int yhi);
void raster_threads_init(int nthreads);
int  raster_threads(void);              /* threads in use */
void raster_threads_set(int n);         /* use only n of the started threads (1..started), takes effect next frame */
void raster_render(Frame *f, BandPre pre, void *ud);   /* also empties the lists */
void raster_attach(Frame *f, RList *opaque, RList *blend);   /* draw these lists too (see Frame.att) */

/* run fn(slot, ud) once on each thread in use (slot 0 = the caller, 1.. the workers) and wait for all of them */
void raster_parallel(void (*fn)(int slot, void *ud), void *ud);

#endif

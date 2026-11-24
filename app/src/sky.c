/* sky.c - gradient sky + textured environment effects, see sky.h for how the original builds them */
#include "sky.h"
#include "lsd.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int g_skyfx_force;

#define SKY_NEAR 0.3f            /* EffectsCamera near clip plane */
#define DEG 0.0174532925f
#define RAY_TIME 0.05f           /* Sun.SUNBURST_UPDATE_TIME_SECONDS */
#define CLOUD_EVENT_TIME 5.0f    /* Cloud.TIME_BETWEEN_EVENTS_SECONDS */

/* ------------------------------------------------------------------ textures (tools/sky_assets.py) */
enum { TEX_SUN, TEX_CLOUDS, TEX_STARS, TEX_N };
typedef struct { int w, h, nlev; const uint8_t *lev[8]; uint8_t *blob; } SkyTex;
static SkyTex g_tex[TEX_N];
static int g_tex_loaded;

static void tex_load(SkyTex *t, const char *path) {
  size_t n; uint8_t *b = lsd_read_file(path, &n);
  memset(t, 0, sizeof *t);
  if (!b || n < 12 || memcmp(b, "SKYT", 4)) { lsd_log("sky: %s missing or bad", path); free(b); return; }
  int w = b[4] << 8 | b[5], h = b[6] << 8 | b[7], nl = b[8];   /* big-endian header (rd16 is the LSD little-endian reader) */
  size_t off = 12;
  for (int l = 0; l < nl && l < 8; l++) {
    size_t sz = (size_t)(w >> l) * (size_t)(h >> l) * 4;
    if (off + sz > n) break;
    t->lev[t->nlev++] = b + off; off += sz;
  }
  if (!t->nlev) { free(b); return; }
  t->w = w; t->h = h; t->blob = b;
}

static void tex_init(void) {
  if (g_tex_loaded) return;
  g_tex_loaded = 1;
  tex_load(&g_tex[TEX_SUN], "sky/sun.bin");
  tex_load(&g_tex[TEX_CLOUDS], "sky/clouds.bin");
  tex_load(&g_tex[TEX_STARS], "sky/stars.bin");
}

/* ------------------------------------------------------------------ gradient sky (LSDRSky.shader) */
static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void env_sky_rows(Frame *f, const Cam *cam, const Env *e, int y0, int y1) {
  /* heightOffset = dot(camera forward, (0, sign(up.y), 0)) pins the band to the horizon; y = screen uv (0 bottom, 1 top) */
  float hoff = sinf(cam->pitch);
  float fh = e->fogheight, gr = e->foggrad > 0.0001f ? e->foggrad : 0.0001f;
  for (int y = y0; y <= y1; y++) {
    float yn = 1.0f - (y + 0.5f) / f->h + hoff;
    float upper = sat((yn - fh * 0.5f - 0.5f) / gr), lower = sat((0.5f - (yn + fh * 0.5f)) / gr);
    float comb = upper + lower;                                 /* never both > 0, so at most 1 */
    uint32_t R = (uint32_t)(sat(e->sky[0] * comb + e->skyfog[0] * (1 - comb)) * 255.0f + 0.5f);
    uint32_t G = (uint32_t)(sat(e->sky[1] * comb + e->skyfog[1] * (1 - comb)) * 255.0f + 0.5f);
    uint32_t B = (uint32_t)(sat(e->sky[2] * comb + e->skyfog[2] * (1 - comb)) * 255.0f + 0.5f);
    uint32_t px = 0xFF000000u | (R << 16) | (G << 8) | B;
    uint32_t *c = f->color + (size_t)y * f->w; float *d = f->depth + (size_t)y * f->w;
    for (int x = 0; x < f->w; x++) { c[x] = px; d[x] = 0.0f; }
  }
}

void env_draw_sky(Frame *f, const Cam *cam, const Env *e) { env_sky_rows(f, cam, e, 0, f->h - 1); }

/* ------------------------------------------------------------------ building (DreamEnvironment.Apply) */
static unsigned xs32(unsigned *s) { unsigned x = *s ? *s : 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static float frand(unsigned *s, float lo, float hi) { return lo + (xs32(s) & 0xFFFFFF) / 16777216.0f * (hi - lo); }
static int irand(unsigned *s, int n) { return (int)(frand(s, 0, 1) * n) % n; }

static void roll_rays(SkySun *s, unsigned *rng) {             /* Sun.updateSunburst */
  for (int i = 0; i < 4; i++) {
    int longray = irand(rng, 2) == 0;                          /* RandUtil.OneIn(2) */
    s->ray_len[i] = (longray ? 4.0f : 2.0f) * s->burst_scale;
    s->ray_ang[i] = frand(rng, 0, 360) * DEG;
  }
}

static void add_quads(SkyFx *fx, int tex, int count, const float tint[3], float alpha, int can_move, unsigned *rng) {
  for (int i = 0; i < count && fx->nquad < SKY_MAX_QUADS; i++) {
    SkyQuad *q = &fx->quad[fx->nquad++]; memset(q, 0, sizeof *q);
    q->pos[0] = frand(rng, -8, 8); q->pos[1] = frand(rng, 2, 3); q->pos[2] = frand(rng, -8, 8);
    q->scale = frand(rng, 1, 4);
    q->yaw = frand(rng, 0, 360) * DEG;
    q->tex = tex; q->tint[0] = tint[0]; q->tint[1] = tint[1]; q->tint[2] = tint[2]; q->tint[3] = alpha;
    if (can_move) q->moves = (g_skyfx_force & 64) ? 1 : irand(rng, 4) == 0;   /* Cloud.Start: RandUtil.OneIn(4) */
  }
}

void skyfx_build(SkyFx *fx, const Env *e, int enabled, int group, int day, unsigned *rng) {
  static int last_day = -1000000; static float root_yaw;
  if (day != last_day) { last_day = day; root_yaw = frand(rng, 0, 360) * DEG; }   /* DreamEnvironmentEffects.randomiseRotation */
  memset(fx, 0, sizeof *fx);
  fx->root_yaw = root_yaw;
  fx->rng = xs32(rng) | 1;
  if (!enabled) return;
  tex_init();
  int force = g_skyfx_force;
  int any_group = group < 0 || (force & 32);
  int sun_ok = any_group || group == 0 || group == 2;          /* Dream.DAY_SKIES, Dream.NIGHT_SKIES */
  int stars_ok = any_group || group == 2;
  /* the dice in the same order as DreamEnvironment.Apply */
  if ((e->cloud_chance > 0 && frand(rng, 0, 1) < e->cloud_chance) || (force & 8))
    add_quads(fx, TEX_CLOUDS, (int)e->nclouds > 0 ? (int)e->nclouds : ((force & 8) ? 6 : 0), e->cloud, 1.0f, 1, rng);
  if ((sun_ok && e->sun_chance > 0 && frand(rng, 0, 1) < e->sun_chance) || (force & 1)) {
    SkySun *s = &fx->sun[fx->nsun++];
    s->pos[0] = 5; s->pos[1] = e->sun_height; s->pos[2] = 0; s->scale = e->sun_size; memcpy(s->tint, e->sun, sizeof s->tint);
    s->burst_scale = 1;
    if ((e->burst_chance > 0 && frand(rng, 0, 1) < e->burst_chance) || (force & 2)) {
      s->burst = 1; s->burst_scale = e->burst_size; memcpy(s->burst_tint, e->burst, sizeof s->burst_tint);
    }
    roll_rays(s, rng);
    if ((e->sun2_chance > 0 && frand(rng, 0, 1) < e->sun2_chance) || (force & 4)) {
      SkySun *s2 = &fx->sun[fx->nsun++];
      s2->pos[0] = 5; s2->pos[1] = e->sun2_height; s2->pos[2] = e->sun2_offset; s2->scale = e->sun2_size; memcpy(s2->tint, e->sun2, sizeof s2->tint);
    }
  }
  if ((stars_ok && e->star_chance > 0 && frand(rng, 0, 1) < e->star_chance) || (force & 16))
    add_quads(fx, TEX_STARS, (int)e->nstars > 0 ? (int)e->nstars : ((force & 16) ? 3 : 0), e->star, 1.0f, 0, rng);
  fx->on = fx->nsun || fx->nquad;
}

void skyfx_update(SkyFx *fx, float dt) {
  if (!fx->on) return;
  if (fx->nsun && fx->sun[0].burst) {                          /* Sun.Update */
    if (fx->ray_t > RAY_TIME) { roll_rays(&fx->sun[0], &fx->rng); fx->ray_t = 0; }
    fx->ray_t += dt;
  }
  for (int i = 0; i < fx->nquad; i++) {                        /* Cloud.Update (per-frame steps of the original as rates) */
    SkyQuad *q = &fx->quad[i];
    if (!q->moves) continue;
    if (q->t > CLOUD_EVENT_TIME) {
      q->t = 0;
      int choice = irand(&fx->rng, 4);
      int rot = choice == 1 || choice == 3, mov = choice == 2 || choice == 3;
      if (choice == 0) { q->vel[0] = q->vel[1] = q->vel[2] = 0; q->rot = 0; }
      if (rot) q->rot = frand(&fx->rng, 20, 50) * DEG;         /* degrees per frame * deltaTime -> degrees per second */
      if (mov) {
        float x = frand(&fx->rng, -1, 1), z = frand(&fx->rng, -1, 1), l = sqrtf(x * x + z * z);
        float sp = frand(&fx->rng, 1, 3);                       /* units per frame * deltaTime -> units per second */
        if (l < 1e-5f) { x = 0; z = 0; l = 1; }
        q->vel[0] = x / l * sp; q->vel[1] = 0; q->vel[2] = z / l * sp;
      }
    }
    q->pos[0] += q->vel[0] * dt; q->pos[2] += q->vel[2] * dt;
    if (q->rot > 0) q->yaw += q->rot * dt;
    q->t += dt;
  }
}

void skyfx_describe(const SkyFx *fx, char *buf, int n) {
  int nc = 0, ns = 0;
  for (int i = 0; i < fx->nquad; i++) { if (fx->quad[i].tex == TEX_CLOUDS) nc++; else ns++; }
  snprintf(buf, (size_t)n, "SUN %d%s CLOUDS %d STARS %d YAW %d", fx->nsun, fx->nsun && fx->sun[0].burst ? " BURST" : "", nc, ns, (int)(fx->root_yaw / DEG));
}

/* ------------------------------------------------------------------ drawing */
typedef struct { float x, y, z, u, v; } SkyCV;                 /* camera space (x right, y up, z forward) + uv */
typedef struct { float x, y, iz, uz, vz; } SkySV;              /* screen */
enum { SH_SUN, SH_BG };
typedef struct { const SkyTex *t; int lev; int mode; float tint[4]; } SkyShade;

static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static inline void shade_px(uint32_t *dst, const SkyShade *sh, const uint8_t *tex, int tw, int th, float u, float v) {
  int tx = (int)(u * tw), ty = (int)((1.0f - v) * th);           /* PNG rows are stored top first; uv v = 0 is the bottom */
  if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
  if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
  const uint8_t *p = tex + ((size_t)ty * tw + tx) * 4;
  if (sh->mode == SH_SUN) {                                    /* col.rgb *= tint * 2; clip(a - 0.5) */
    if (p[3] * sh->tint[3] < 127.5f) return;
    int R = clamp255((int)(p[0] * sh->tint[0] * 2.0f)), G = clamp255((int)(p[1] * sh->tint[1] * 2.0f)), B = clamp255((int)(p[2] * sh->tint[2] * 2.0f));
    *dst = 0xFF000000u | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
  } else {                                                     /* saturate(tex * 2) * tint, alpha = tex.a * tint.a, SrcAlpha/OneMinusSrcAlpha */
    int a = (int)(p[3] * sh->tint[3] + 0.5f);
    if (a <= 0) return;
    int sr = (int)((p[0] * 2 > 255 ? 255 : p[0] * 2) * sh->tint[0]), sg = (int)((p[1] * 2 > 255 ? 255 : p[1] * 2) * sh->tint[1]), sb = (int)((p[2] * 2 > 255 ? 255 : p[2] * 2) * sh->tint[2]);
    uint32_t d = *dst; int dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
    int R = dr + ((sr - dr) * a + 127) / 255, G = dg + ((sg - dg) * a + 127) / 255, B = db + ((sb - db) * a + 127) / 255;
    *dst = 0xFF000000u | (uint32_t)clamp255(R) << 16 | (uint32_t)clamp255(G) << 8 | (uint32_t)clamp255(B);
  }
}

/* perspective-correct textured triangle restricted to rows ylo..yhi; u, v exact every 8 pixels, linear between */
static void sky_tri(Frame *f, const SkySV *a, const SkySV *b, const SkySV *c, const SkyShade *sh, int ylo, int yhi) {
  const SkySV *t = a, *m = b, *o = c, *tmp;
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  if (m->y > o->y) { tmp = m; m = o; o = tmp; }
  if (t->y > m->y) { tmp = t; t = m; m = tmp; }
  float area = (m->x - t->x) * (o->y - t->y) - (o->x - t->x) * (m->y - t->y);
  if (area > -1e-4f && area < 1e-4f) return;
  int y0 = (int)ceilf(t->y - 0.5f), y1 = (int)ceilf(o->y - 0.5f) - 1;
  if (y0 < ylo) y0 = ylo;
  if (y1 > yhi) y1 = yhi;
  if (y0 > y1) return;
  float inv = 1.0f / area;
  float A0[3] = { t->iz, t->uz, t->vz }, Am[3] = { m->iz, m->uz, m->vz }, Ab[3] = { o->iz, o->uz, o->vz }, gx[3], gy[3];
  float dym = m->y - t->y, dyb = o->y - t->y, dxm = m->x - t->x, dxb = o->x - t->x;
  for (int k = 0; k < 3; k++) {
    float pm = Am[k] - A0[k], pb = Ab[k] - A0[k];
    gx[k] = (pm * dyb - pb * dym) * inv; gy[k] = (pb * dxm - pm * dxb) * inv;
  }
  float sL = dyb > 0 ? dxb / dyb : 0, sA = dym > 0 ? dxm / dym : 0, sB = (o->y - m->y) > 0 ? (o->x - m->x) / (o->y - m->y) : 0;
  const SkyTex *tx = sh->t; const uint8_t *tex = tx->lev[sh->lev]; int tw = tx->w >> sh->lev, th = tx->h >> sh->lev;
  for (int y = y0; y <= y1; y++) {
    float yc = y + 0.5f;
    float xa = t->x + sL * (yc - t->y), xb = yc < m->y ? t->x + sA * (yc - t->y) : m->x + sB * (yc - m->y);
    float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
    int xs = (int)ceilf(xl - 0.5f), xe = (int)ceilf(xr - 0.5f) - 1;
    if (xs < 0) xs = 0;
    if (xe > f->w - 1) xe = f->w - 1;
    if (xs > xe) continue;
    float ox = xs + 0.5f - t->x, oy = yc - t->y;
    float iz = A0[0] + gx[0] * ox + gy[0] * oy, uz = A0[1] + gx[1] * ox + gy[1] * oy, vz = A0[2] + gx[2] * ox + gy[2] * oy;
    uint32_t *row = f->color + (size_t)y * f->w;
    float z = 1.0f / iz, u0 = uz * z, v0 = vz * z;
    for (int x = xs; x <= xe; ) {
      int n = xe - x + 1; if (n > 8) n = 8;
      float iz1 = iz + gx[0] * n, uz1 = uz + gx[1] * n, vz1 = vz + gx[2] * n;
      float z1 = 1.0f / iz1, u1 = uz1 * z1, v1 = vz1 * z1;
      float du = (u1 - u0) / n, dv = (v1 - v0) / n, u = u0, v = v0;
      for (int k = 0; k < n; k++, u += du, v += dv) shade_px(&row[x + k], sh, tex, tw, th, u, v);
      x += n; iz = iz1; uz = uz1; vz = vz1; u0 = u1; v0 = v1;
    }
  }
}

/* clip a camera-space triangle at the near plane, project, pick the texture level, rasterize */
static void draw_cv_tri(Frame *f, const Cam *cam, const SkyCV *in, SkyShade sh, int ylo, int yhi) {
  SkyCV poly[4]; int n = 0;
  for (int i = 0; i < 3; i++) {
    const SkyCV *p = &in[i], *q = &in[(i + 1) % 3];
    int pin = p->z >= SKY_NEAR, qin = q->z >= SKY_NEAR;
    if (pin) poly[n++] = *p;
    if (pin != qin) {
      float t = (SKY_NEAR - p->z) / (q->z - p->z); SkyCV r;
      r.x = p->x + (q->x - p->x) * t; r.y = p->y + (q->y - p->y) * t; r.z = SKY_NEAR;
      r.u = p->u + (q->u - p->u) * t; r.v = p->v + (q->v - p->v) * t; poly[n++] = r;
    }
  }
  if (n < 3) return;
  float cx = f->w * 0.5f, cy = f->h * 0.5f;
  SkySV s[4];
  for (int i = 0; i < n; i++) {
    float iz = 1.0f / poly[i].z;
    s[i].x = cx + cam->focal * poly[i].x * iz; s[i].y = cy - cam->focal * poly[i].y * iz;
    s[i].iz = iz; s[i].uz = poly[i].u * iz; s[i].vz = poly[i].v * iz;
  }
  if (sh.mode == SH_BG && sh.t->nlev > 1) {                   /* minified: use the baked smaller level (see sky_assets.py) */
    float sa = fabsf((s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y));
    float ta = fabsf((in[1].u - in[0].u) * (in[2].v - in[0].v) - (in[2].u - in[0].u) * (in[1].v - in[0].v)) * sh.t->w * sh.t->h;
    /* screen area of the whole (unclipped) triangle: project all three if in front, else keep level 0 */
    if (in[0].z > SKY_NEAR && in[1].z > SKY_NEAR && in[2].z > SKY_NEAR && n == 3 && sa > 0.5f) {
      float r = sqrtf(ta / sa); int l = 0;
      while (r >= 2.0f && l < sh.t->nlev - 1) { r *= 0.5f; l++; }
      sh.lev = l;
    }
  }
  sky_tri(f, &s[0], &s[1], &s[2], &sh, ylo, yhi);
  if (n == 4) sky_tri(f, &s[0], &s[2], &s[3], &sh, ylo, yhi);
}

/* Unity's built-in Quad: vertices (-.5,-.5) (.5,-.5) (-.5,.5) (.5,.5), uv = vertex + .5, triangles 0,3,1 and 3,0,2 */
static const float QV[4][2] = { {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} };
static const int QT[2][3] = { {0, 3, 1}, {3, 0, 2} };

typedef struct { float R[3], U[3], F[3], cr, sr; } SkyView;

static inline void to_cam(const SkyView *vw, const float p[3], float o[3]) {
  o[0] = vw->R[0] * p[0] + vw->R[1] * p[1] + vw->R[2] * p[2];
  o[1] = vw->U[0] * p[0] + vw->U[1] * p[1] + vw->U[2] * p[2];
  o[2] = vw->F[0] * p[0] + vw->F[1] * p[1] + vw->F[2] * p[2];
}
/* root-local -> world offset from the camera: Unity yaw rotation (x, z) -> (x cos + z sin, -x sin + z cos) */
static inline void root_rot(const SkyView *vw, const float p[3], float o[3]) {
  o[0] = p[0] * vw->cr + p[2] * vw->sr; o[1] = p[1]; o[2] = -p[0] * vw->sr + p[2] * vw->cr;
}

/* LSDR/Sun: view-space billboard. Corners = view(centre) + rotZ(angle) * (vx * sx, vy * sy). */
static void draw_billboard(Frame *f, const Cam *cam, const SkyView *vw, const float pos[3], float sx, float sy, float ang, int thin,
                           const float tint[3], int ylo, int yhi) {
  if (!g_tex[TEX_SUN].nlev) return;
  float w[3], c[3]; root_rot(vw, pos, w); to_cam(vw, w, c);
  if (c[2] < SKY_NEAR) return;                                 /* the whole billboard is at the centre's depth */
  float vscale = 1.0f;
  if (thin) {                                                  /* sunburst rays are thinner than a pixel at 240 lines: keep 1 px */
    float px = cam->focal * sy * 0.5f / c[2];                  /* visible thickness: the disc covers half the texture */
    if (px < 1.0f && px > 0) { vscale = 1.0f / px; sy *= vscale; }
  }
  float cs = cosf(ang), sn = sinf(ang);
  SkyCV v[4];
  for (int i = 0; i < 4; i++) {
    float ox = QV[i][0] * sx, oy = QV[i][1] * sy;
    v[i].x = c[0] + cs * ox - sn * oy; v[i].y = c[1] + sn * ox + cs * oy; v[i].z = c[2];
    v[i].u = QV[i][0] + 0.5f; v[i].v = 0.5f + QV[i][1] / vscale;
  }
  SkyShade sh = { &g_tex[TEX_SUN], 0, SH_SUN, { tint[0], tint[1], tint[2], 1.0f } };
  for (int t = 0; t < 2; t++) { SkyCV tri[3] = { v[QT[t][0]], v[QT[t][1]], v[QT[t][2]] }; draw_cv_tri(f, cam, tri, sh, ylo, yhi); }
}

static void draw_sun(Frame *f, const Cam *cam, const SkyView *vw, const SkySun *s, int ylo, int yhi) {
  float h = s->scale * 0.5f;                                   /* Sun.SetScale: _ScaleX = _ScaleY = scale / 2 */
  draw_billboard(f, cam, vw, s->pos, h, h, 0, 0, s->tint, ylo, yhi);
}

static void draw_rays(Frame *f, const Cam *cam, const SkyView *vw, const SkySun *s, int ylo, int yhi) {
  for (int i = 0; i < 4; i++)                                  /* _ScaleX = (4|2) * scale, _ScaleY = scale / 16, _RotationAngle */
    draw_billboard(f, cam, vw, s->pos, s->ray_len[i], s->burst_scale / 16.0f, s->ray_ang[i], 1, s->burst_tint, ylo, yhi);
}

static void draw_quad(Frame *f, const Cam *cam, const SkyView *vw, const SkyQuad *q, float cutoff, int ylo, int yhi) {
  const SkyTex *tx = &g_tex[q->tex];
  if (!tx->nlev || q->tint[3] <= 0) return;
  float cy = cosf(q->yaw), sy = sinf(q->yaw);
  SkyCV v[4]; float dist[4];
  for (int i = 0; i < 4; i++) {
    /* object (x, y, 0) -> Rx(-90): (x, 0, -y) -> scale -> own yaw -> + pos -> root yaw -> camera */
    float lx = QV[i][0] * q->scale, lz = -QV[i][1] * q->scale;
    float p[3] = { q->pos[0] + lx * cy + lz * sy, q->pos[1], q->pos[2] - lx * sy + lz * cy }, w[3], c[3];
    root_rot(vw, p, w); to_cam(vw, w, c);
    dist[i] = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    v[i].x = c[0]; v[i].y = c[1]; v[i].z = c[2]; v[i].u = QV[i][0] + 0.5f; v[i].v = QV[i][1] + 0.5f;
  }
  SkyShade sh = { tx, 0, SH_BG, { q->tint[0], q->tint[1], q->tint[2], q->tint[3] } };
  for (int t = 0; t < 2; t++) {
    const int *ix = QT[t];
    /* LSDR.cginc: a vertex past min(fog end, draw distance) goes behind the far plane, which clips its triangle away */
    if (dist[ix[0]] > cutoff || dist[ix[1]] > cutoff || dist[ix[2]] > cutoff) continue;
    SkyCV tri[3] = { v[ix[0]], v[ix[1]], v[ix[2]] };
    draw_cv_tri(f, cam, tri, sh, ylo, yhi);
  }
}

void skyfx_band(Frame *f, const Cam *cam, const SkyFx *fx, int y0, int y1) {
  if (!fx || !fx->on) return;
  SkyView vw; cam_basis(cam, vw.R, vw.U, vw.F); vw.cr = cosf(fx->root_yaw); vw.sr = sinf(fx->root_yaw);
  float cutoff = cam->sky_cut > 0 ? cam->sky_cut : cam->draw_dist - 1.0f;   /* main.c: min(20, fog end), the classic view, whatever the view distance */
  if (cutoff <= 0) cutoff = 20.0f;
  /* queue Background-1: the first sun's rays */
  if (fx->nsun && fx->sun[0].burst) draw_rays(f, cam, &vw, &fx->sun[0], y0, y1);
  /* queue Background: first sun + clouds + stars, Unity's opaque-range order (front to back by distance) */
  int order[SKY_MAX_QUADS + 1]; float key[SKY_MAX_QUADS + 1]; int n = 0;
  if (fx->nsun) { order[n] = -1; key[n] = fx->sun[0].pos[0] * fx->sun[0].pos[0] + fx->sun[0].pos[1] * fx->sun[0].pos[1] + fx->sun[0].pos[2] * fx->sun[0].pos[2]; n++; }
  for (int i = 0; i < fx->nquad; i++) { const float *p = fx->quad[i].pos; order[n] = i; key[n] = p[0] * p[0] + p[1] * p[1] + p[2] * p[2]; n++; }
  for (int i = 1; i < n; i++) { int o = order[i]; float k = key[i]; int j = i - 1; while (j >= 0 && key[j] > k) { order[j + 1] = order[j]; key[j + 1] = key[j]; j--; } order[j + 1] = o; key[j + 1] = k; }
  for (int i = 0; i < n; i++) {
    if (order[i] < 0) draw_sun(f, cam, &vw, &fx->sun[0], y0, y1);
    else draw_quad(f, cam, &vw, &fx->quad[order[i]], cutoff, y0, y1);
  }
  /* queue Background+1: the second sun, over everything else */
  if (fx->nsun > 1) draw_sun(f, cam, &vw, &fx->sun[1], y0, y1);
}

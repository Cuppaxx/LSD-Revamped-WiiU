#include "render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR_Z 0.03f

typedef struct { float x, y, z, u, v, r, g, b, d; } CV;     /* camera-space vertex */

float g_snap_step = 1.0f;   /* vertex snapping grid in pixels (Frame height / 240, the PS1 grid at any resolution); 0 = off */
/* texture warping (Revamped GameSettings.AffineIntensity, default 0.5): q = (1 - 0.75 I) / z + I, see raster.c */
static float aff_a = 0.625f, aff_b = 0.5f;
void render_set_affine(float i) { if (i < 0) i = 0; if (i > 1) i = 1; aff_a = 1.0f - 0.75f * i; aff_b = i; }
static inline void to_sv(const CV *c, float focal, SVtx *o, float cx, float cy) {
  float iz = 1.0f / c->z;
  o->q = aff_a * iz + aff_b;
  float x = cx + focal * c->x * iz, y = cy - focal * c->y * iz;
  if (g_snap_step >= 1.0f) {                     /* PS1-style vertex snapping (the wobble) */
    if (g_snap_step == 1.0f) { x = floorf(x); y = floorf(y); }
    else { float s = g_snap_step, is = 1.0f / s; x = floorf(x * is) * s; y = floorf(y * is) * s; }
  }
  o->x = x; o->y = y;
  o->d = c->d; o->iz = iz; o->u = c->u; o->v = c->v; o->r = c->r; o->g = c->g; o->b = c->b;
}

static inline CV lerp_cv(const CV *a, const CV *b, float t) {
  CV o;
  o.x = a->x + (b->x - a->x) * t; o.y = a->y + (b->y - a->y) * t; o.z = a->z + (b->z - a->z) * t;
  o.u = a->u + (b->u - a->u) * t; o.v = a->v + (b->v - a->v) * t;
  o.r = a->r + (b->r - a->r) * t; o.g = a->g + (b->g - a->g) * t; o.b = a->b + (b->b - a->b) * t;
  o.d = a->d + (b->d - a->d) * t;
  return o;
}

static const WaterSet *g_water; static int g_texset;
void render_set_water(const WaterSet *ws, int texset) { g_water = ws; g_texset = texset; }

static void emit_tri(Frame *f, const Cam *cam, const CV *a, const CV *b, const CV *c, const uint32_t *atlas, int water, int blend, RenderStats *st) {
  CV in[3] = {*a, *b, *c};
  CV poly[4]; int n = 0;
  for (int i = 0; i < 3; i++) {
    const CV *p = &in[i], *q = &in[(i + 1) % 3];
    int pin = p->z >= NEAR_Z, qin = q->z >= NEAR_Z;
    if (pin) poly[n++] = *p;
    if (pin != qin) { float t = (NEAR_Z - p->z) / (q->z - p->z); poly[n++] = lerp_cv(p, q, t); poly[n - 1].z = NEAR_Z; }
  }
  if (n < 3) return;
  SVtx s[4];
  for (int i = 0; i < n; i++) to_sv(&poly[i], cam->focal, &s[i], (float)(f->w / 2), (float)(f->h / 2));
  const WaterTex *wt = water && g_water && water <= g_water->n ? &g_water->t[water - 1] : NULL; const uint8_t *pal = wt ? wt->pal[g_texset & 3] : NULL;
  raster_submit(f, &s[0], &s[1], &s[2], atlas, wt, pal, blend);
  if (n == 4) raster_submit(f, &s[0], &s[2], &s[3], atlas, wt, pal, blend);
  st->tris_drawn += n - 2;
}

void render_chunk(Frame *f, const Cam *cam, const LChunk *c, const LAtlas *atlas, float ox, float oz, RenderStats *st) {
  float cy_ = cosf(cam->yaw), sy_ = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
  float R[3] = {cy_, 0, -sy_};                      /* right */
  float U[3] = {-sy_ * sp, cp, -cy_ * sp};          /* up */
  float F[3] = {sy_ * cp, sp, cy_ * cp};            /* forward */
  float halfx = (f->w * 0.5f) / cam->focal + 0.1f;
  int total = c->w * c->h;
  for (int idx = 0; idx < total + c->nextra; idx++) {
    const LTile *t0 = idx < total ? &c->tiles[idx] : &c->extra[idx - total];
    /* extra tiles are drawn as part of their parent's chain below; skip them here */
    if (idx >= total) break;
    int x = idx % c->w, y = idx / c->w;
    const LTile *chain[3]; int nchain = 0;
    const LTile *t = t0;
    if (!t->draw) continue;
    chain[nchain++] = t;
    int guard = 0;
    while (t->extra >= 0 && t->extra < c->nextra && guard < 2) { t = &c->extra[t->extra]; chain[nchain++] = t; guard++; }
    for (int ci = 0; ci < nchain; ci++) {
      const LTile *tt = chain[ci];
      if (tt->type >= c->nmesh) continue;
      const LMesh *m = &c->mesh[tt->type];
      if (!m->ntris) continue;
      float tx = x + ox + 0.5f, ty = -(float)tt->height, tz = y + oz + 0.5f;   /* +0.5: tile centre for culling */
      (void)tx; (void)tz;
      float wx = x + ox, wy = ty, wz = y + oz;
      /* cull by tile centre */
      float dx = wx - cam->x, dy = wy - cam->y, dz = wz - cam->z;
      float ccx = dx * R[0] + dy * R[1] + dz * R[2], ccz = dx * F[0] + dy * F[1] + dz * F[2];
      if (ccz < -1.8f) continue;
      float dist2 = dx * dx + dz * dz;
      if (dist2 > cam->draw_dist * cam->draw_dist) continue;
      if (fabsf(ccx) > (ccz > 0 ? ccz : 0) * halfx + 2.2f) continue;
      st->tiles_drawn++;
      /* rotation about Y (Unity convention) */
      float cs, sn;
      switch (tt->dir & 3) { case 1: cs = 0; sn = 1; break; case 2: cs = -1; sn = 0; break; case 3: cs = 0; sn = -1; break; default: cs = 1; sn = 0; }
      /* world = Ry*v + (wx,wy,wz); Ry columns: x->(cs,0,-sn), y->(0,1,0), z->(sn,0,cs) */
      float col[3][3] = {{cs, 0, -sn}, {0, 1, 0}, {sn, 0, cs}};
      float M[3][3];                                 /* camera-space = rows(R,U,F) * Ry */
      for (int k = 0; k < 3; k++) {
        M[0][k] = R[0] * col[k][0] + R[1] * col[k][1] + R[2] * col[k][2];
        M[1][k] = U[0] * col[k][0] + U[1] * col[k][1] + U[2] * col[k][2];
        M[2][k] = F[0] * col[k][0] + F[1] * col[k][1] + F[2] * col[k][2];
      }
      float ex = wx - cam->x, ey = wy - cam->y, ez = wz - cam->z;
      float o0 = R[0] * ex + R[1] * ey + R[2] * ez, o1 = U[0] * ex + U[1] * ey + U[2] * ez, o2 = F[0] * ex + F[1] * ey + F[2] * ez;
      for (int i = 0; i < m->ntris; i++) {
        const LTri *tr = &m->tris[i];
        CV cv[3];
        int behind = 0;
        for (int k = 0; k < 3; k++) {
          const LVtx *v = &tr->v[k];
          cv[k].x = M[0][0] * v->x + M[0][1] * v->y + M[0][2] * v->z + o0;
          cv[k].y = M[1][0] * v->x + M[1][1] * v->y + M[1][2] * v->z + o1;
          cv[k].z = M[2][0] * v->x + M[2][1] * v->y + M[2][2] * v->z + o2;
          cv[k].u = v->u; cv[k].v = v->v; cv[k].r = v->r; cv[k].g = v->g; cv[k].b = v->b;
          cv[k].d = sqrtf(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
          if (cv[k].z < NEAR_Z) behind++;
        }
        if (behind == 3) continue;
        const uint32_t *tex = tr->v[0].u >= 0 ? atlas->px : NULL;
        emit_tri(f, cam, &cv[0], &cv[1], &cv[2], tex, tr->water, tr->alpha_blend, st);
      }
    }
  }
}

void chunk_bounds(const LChunk *c, float ox, float oz, float mn[3], float mx[3]) {
  mn[0] = mn[1] = mn[2] = 1e9f; mx[0] = mx[1] = mx[2] = -1e9f;
  for (int idx = 0; idx < c->w * c->h; idx++) {
    const LTile *t = &c->tiles[idx];
    if (!t->draw || t->type >= c->nmesh) continue;
    int x = idx % c->w, y = idx / c->w;
    const LMesh *m = &c->mesh[t->type];
    for (int i = 0; i < m->ntris; i++) for (int k = 0; k < 3; k++) {
      const LVtx *v = &m->tris[i].v[k];
      float wx = x + ox + v->x, wy = -(float)t->height + v->y, wz = y + oz + v->z;   /* ignores rotation: bounds only */
      if (wx < mn[0]) mn[0] = wx; if (wx > mx[0]) mx[0] = wx;
      if (wy < mn[1]) mn[1] = wy; if (wy > mx[1]) mx[1] = wy;
      if (wz < mn[2]) mn[2] = wz; if (wz > mx[2]) mx[2] = wz;
    }
  }
  if (mn[0] > mx[0]) { mn[0] = mn[1] = mn[2] = 0; mx[0] = mx[1] = mx[2] = 1; }
}

/* ---- 3x5 debug font ---- */
static const struct { char c; const char *rows; } font[] = {
  {'0', "111101101101111"}, {'1', "010110010010111"}, {'2', "111001111100111"}, {'3', "111001111001111"}, {'4', "101101111001001"},
  {'5', "111100111001111"}, {'6', "111100111101111"}, {'7', "111001001001001"}, {'8', "111101111101111"}, {'9', "111101111001111"},
  {'F', "111100110100100"}, {'P', "111101111100100"}, {'S', "111100111001111"}, {'T', "111010010010010"}, {'R', "110101110101101"},
  {'I', "111010010010111"}, {'M', "101111111101101"}, {'C', "111100100100111"}, {'H', "101101111101101"}, {'-', "000000111000000"},
  {':', "000010000010000"}, {'A', "010101111101101"}, {'L', "100100100100111"}, {'D', "110101101101110"}, {'U', "101101101101111"},
  {'/', "001001010100100"}, {'X', "101101010101101"}, {'E', "111100110100111"}, {'N', "111101101101101"}, {'O', "111101101101111"},
  {'.', "000000000000010"}, {'B', "110101110101110"}, {'G', "111100101101111"}, {'W', "101101111111101"}, {'Y', "101101010010010"},
  {'=', "000111000111000"}, {',', "000000000010100"}, {'%', "101001010100101"}, {'V', "101101101101010"}, {'Z', "111001010100111"}, {'K', "101101110101101"}, {'J', "001001001101111"}, {'Q', "111101101111001"},
};

void draw_text(Frame *f, int x, int y, const char *s, uint32_t argb) {
  for (; *s; s++, x += 4) {
    const char *rows = NULL; char ch = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
    for (unsigned i = 0; i < sizeof font / sizeof font[0]; i++) if (font[i].c == ch) { rows = font[i].rows; break; }
    if (!rows) continue;
    for (int r = 0; r < 5; r++) for (int cc = 0; cc < 3; cc++) if (rows[r * 3 + cc] == '1') {
      int px = x + cc, py = y + r;
      if (px >= 0 && px < f->w && py >= 0 && py < f->h) f->color[py * f->w + px] = argb;
    }
  }
}

/* ---- environment: fog table (LSDRFog.cginc GetFogDepth / classic stepping) and gradient sky (LSDRSky.shader) ---- */
static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void env_apply_fog(Frame *f, const Env *e) {
  f->fog_on = 1; f->fog_scale = 255.0f / fmaxf(64.0f, e->end * 1.05f);   /* lookup covers 0..64 tiles (or the fog end, if further) */
  float start = fmaxf(e->start, 0.001f), end = fmaxf(e->end, start + 0.001f);
  for (int i = 0; i < 256; i++) {
    float d = i / f->fog_scale;
    float fog = (1.0f - start / fmaxf(d, start)) / (1.0f - start / end);
    float clear = (1.0f - sat(fog)); clear *= clear;
    clear = 1.0f - floorf((1.0f - clear) * 8.0f) / 8.0f;     /* classic: a few coarse steps */
    float inv = 1.0f - clear;
    float mul = e->sub ? clear : 1.0f, w = e->sub ? inv * inv : inv;  /* additive: c + fog*(1-clear); subtractive: c*clear + fog*(1-clear)^2 */
    f->fog_mul[i] = (uint16_t)(mul * 256.0f);
    for (int k = 0; k < 3; k++) f->fog_add[i][k] = (uint8_t)(sat(e->fog[k] * w) * 255.0f);
  }
}

/* gradient sky and sky effects: sky.c */

void render_mesh(Frame *f, const Cam *cam, const LMesh *m, float wx, float wy, float wz, float yaw, RenderStats *st) {
  /* world = Ry(yaw) * v + (wx,wy,wz); Ry columns x->(cs,0,-sn), z->(sn,0,cs) (Unity convention) */
  float cs = cosf(yaw), sn = sinf(yaw);
  const float W[12] = { cs, 0, sn, wx,   0, 1, 0, wy,   -sn, 0, cs, wz };
  render_mesh_xf(f, cam, m, W, NULL, st);
}

/* ---- mesh dreams (meshworld.h): generic textures, materials, chunk culling ---- */

/* near-plane clip, project, back-face cull (cull: drop triangles that are not clockwise on screen, Unity's front face),
 * then the generic texture path */
static void emit_tri_tex(Frame *f, const Cam *cam, const CV *a, const CV *b, const CV *c, const RTex *rt, int va, int cull, RenderStats *st) {
  SVtx s[4]; int n = 0;
  float hw = (float)(f->w / 2), hh = (float)(f->h / 2);
  if (a->z >= NEAR_Z && b->z >= NEAR_Z && c->z >= NEAR_Z) {     /* the common case: nothing to clip */
    to_sv(a, cam->focal, &s[0], hw, hh); to_sv(b, cam->focal, &s[1], hw, hh); to_sv(c, cam->focal, &s[2], hw, hh); n = 3;
  } else {
    const CV *in[3] = {a, b, c};
    CV poly[4];
    for (int i = 0; i < 3; i++) {
      const CV *p = in[i], *q = in[(i + 1) % 3];
      int pin = p->z >= NEAR_Z, qin = q->z >= NEAR_Z;
      if (pin) poly[n++] = *p;
      if (pin != qin) { float t = (NEAR_Z - p->z) / (q->z - p->z); poly[n++] = lerp_cv(p, q, t); poly[n - 1].z = NEAR_Z; }
    }
    if (n < 3) return;
    for (int i = 0; i < n; i++) to_sv(&poly[i], cam->focal, &s[i], hw, hh);
  }
  if (cull) {                                            /* screen y points down: clockwise on screen = positive cross */
    float cr = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
    if (n == 4 && cr == 0.0f) cr = (s[2].x - s[0].x) * (s[3].y - s[0].y) - (s[3].x - s[0].x) * (s[2].y - s[0].y);
    if (cr <= 0.0f) return;
  }
  raster_submit_tex(f, &s[0], &s[1], &s[2], rt, va);
  if (n == 4) raster_submit_tex(f, &s[0], &s[2], &s[3], rt, va);
  st->tris_drawn += n - 2;
}

/* u,v (0..1, repeat) -> texels of the material's texture, shifted by whole repeats so the 16.16 rasterizer never overflows */
static inline void set_uv(CV *cv, const float *uv, const RTex *rt) {
  float u0 = floorf(fminf(uv[0], fminf(uv[2], uv[4]))), v0 = floorf(fminf(uv[1], fminf(uv[3], uv[5])));
  for (int k = 0; k < 3; k++) { cv[k].u = (uv[k * 2] - u0) * rt->w; cv[k].v = (uv[k * 2 + 1] - v0) * rt->h; }
}

float g_mw_detail = 1.0f;   /* reserved: fraction of the view distance for detail triangles */

/* the visible chunks are projected by all render threads at once (raster_parallel): each thread takes chunks one by one
 * and fills triangle lists of its own (a copy of the Frame header), which are appended to the frame's lists afterwards */
#define MW_SLOTS 6
typedef struct {
  Frame *f; const Cam *cam; const MeshWorld *mw;
  float R[3], U[3], F[3], hx, hy, far2;
  int *vis, nvis; int next;
  Frame sf[MW_SLOTS]; RenderStats st[MW_SLOTS];
} MWJob;
static MWJob mwj;
static RList slot_lists[MW_SLOTS][2];

static void mw_chunk(MWJob *j, Frame *f, const MChunk *ch, RenderStats *st) {
  const float *R = j->R, *U = j->U, *F = j->F; const float hx = j->hx, hy = j->hy, far2 = j->far2;
  const float ex = j->cam->x, ey = j->cam->y, ez = j->cam->z; const MeshWorld *mw = j->mw; const Cam *cam = j->cam;
  {
    for (int i = 0; i < ch->ndrawn; i++) {
      const MWTri *t = &ch->tris[i];
      CV cv[3]; int ox = 0x3F, mind = 1;                     /* outcode bits: right left top bottom near; all-far */
      for (int k = 0; k < 3; k++) {
        float px = t->p[k * 3] - ex, py = t->p[k * 3 + 1] - ey, pz = t->p[k * 3 + 2] - ez;
        float x = R[0] * px + R[2] * pz, y = U[0] * px + U[1] * py + U[2] * pz, z = F[0] * px + F[1] * py + F[2] * pz;
        int code = (x > z * hx) | (x < -z * hx) << 1 | (y > z * hy) << 2 | (y < -z * hy) << 3 | (z < NEAR_Z) << 4;
        float d2 = x * x + y * y + z * z;
        if (d2 <= far2) mind = 0; else code |= 32;
        ox &= code;
        cv[k].x = x; cv[k].y = y; cv[k].z = z; cv[k].d = d2;
      }
      if (ox || mind) continue;
      const MMat *m = &mw->mat[t->mat]; const RTex *rt = &m->rt;
      for (int k = 0; k < 3; k++) { cv[k].d = sqrtf(cv[k].d); cv[k].r = t->c[k * 4]; cv[k].g = t->c[k * 4 + 1]; cv[k].b = t->c[k * 4 + 2]; }
      set_uv(cv, t->uv, rt);
      int va = rt->mode == RT_BLEND ? (t->c[3] + t->c[7] + t->c[11]) * 256 / 765 : 256;
      emit_tri_tex(f, cam, &cv[0], &cv[1], &cv[2], rt, va, m->cull == 2, st);
    }
  }
}

static void mw_slot(int slot, void *ud) {
  MWJob *j = (MWJob *)ud; if (slot >= MW_SLOTS) return;
  Frame *sf = &j->sf[slot]; RenderStats *st = &j->st[slot];
  for (;;) {
    int k = __atomic_fetch_add(&j->next, 1, __ATOMIC_RELAXED);
    if (k >= j->nvis) break;
    mw_chunk(j, sf, &j->mw->chunks[j->vis[k]], st);
  }
}

static void list_append(RList *dst, const RList *src) {
  if (!src->n) return;
  if (dst->n + src->n > dst->cap) { int c = dst->cap ? dst->cap : 4096; while (c < dst->n + src->n) c *= 2; dst->t = (RTri *)realloc(dst->t, (size_t)c * sizeof(RTri)); dst->cap = c; }
  memcpy(dst->t + dst->n, src->t, (size_t)src->n * sizeof(RTri)); dst->n += src->n;
}

void render_meshworld(Frame *f, const Cam *cam, const MeshWorld *mw, RenderStats *st) {
  if (!mw || !mw->loaded) return;
  MWJob *j = &mwj;
  j->f = f; j->cam = cam; j->mw = mw;
  float *R = j->R, *U = j->U, *F = j->F; cam_basis(cam, R, U, F);
  j->hx = (f->w * 0.5f) / cam->focal; j->hy = (f->h * 0.5f) / cam->focal;
  float far = cam->draw_dist; j->far2 = far * far;
  /* side planes through the eye, normals pointing out: inside <=> dot(n, p - eye) <= 0 */
  float P[5][3];
  for (int k = 0; k < 3; k++) {
    P[0][k] = R[k] - j->hx * F[k]; P[1][k] = -R[k] - j->hx * F[k]; P[2][k] = U[k] - j->hy * F[k]; P[3][k] = -U[k] - j->hy * F[k]; P[4][k] = -F[k];
  }
  const float ex = cam->x, ey = cam->y, ez = cam->z;
  static int *vis; static int viscap;
  if (viscap < mw->nchunks) { viscap = mw->nchunks; vis = (int *)realloc(vis, sizeof(int) * (size_t)viscap); }
  j->vis = vis; j->nvis = 0; j->next = 0;
  for (int ci = 0; ci < mw->nchunks; ci++) {
    const MChunk *ch = &mw->chunks[ci];
    if (!ch->ndrawn) continue;
    /* 1) the chunk's box against the view distance (= fog end at most) */
    float dx = fmaxf(fmaxf(ch->mn[0] - ex, ex - ch->mx[0]), 0.0f), dy = fmaxf(fmaxf(ch->mn[1] - ey, ey - ch->mx[1]), 0.0f), dz = fmaxf(fmaxf(ch->mn[2] - ez, ez - ch->mx[2]), 0.0f);
    if (dx * dx + dy * dy + dz * dz > j->far2) continue;
    /* 2) against the view frustum: outside if its most inside corner is outside one plane */
    int out = 0;
    for (int p = 0; p < 5 && !out; p++) {
      float vx = P[p][0] > 0 ? ch->mn[0] : ch->mx[0], vy = P[p][1] > 0 ? ch->mn[1] : ch->mx[1], vz = P[p][2] > 0 ? ch->mn[2] : ch->mx[2];
      if (P[p][0] * (vx - ex) + P[p][1] * (vy - ey) + P[p][2] * (vz - ez) > 0.05f) out = 1;
    }
    if (out) continue;
    vis[j->nvis++] = ci;                               /* 3) per triangle, in mw_chunk */
  }
  st->tiles_drawn += j->nvis;
  for (int s = 0; s < MW_SLOTS; s++) {
    j->sf[s] = *f; j->sf[s].lists[0] = slot_lists[s][0]; j->sf[s].lists[1] = slot_lists[s][1];
    j->sf[s].lists[0].n = j->sf[s].lists[1].n = 0; memset(&j->st[s], 0, sizeof j->st[s]);
  }
  if (j->nvis > 1) raster_parallel(mw_slot, j); else mw_slot(0, j);
  for (int s = 0; s < MW_SLOTS; s++) {
    slot_lists[s][0] = j->sf[s].lists[0]; slot_lists[s][1] = j->sf[s].lists[1];   /* keep the grown buffers */
    list_append(&f->lists[0], &slot_lists[s][0]); list_append(&f->lists[1], &slot_lists[s][1]);
    st->tris_drawn += j->st[s].tris_drawn;
  }
}

/* an animated model mesh of a mesh dream (amodels.bin): world matrix W, materials of the dream's world.bin */
void render_mesh_mw(Frame *f, const Cam *cam, const LMesh *m, const float W[12], const MeshWorld *mw, RenderStats *st) {
  float R[3], U[3], F[3]; cam_basis(cam, R, U, F);
  float M[3][3];
  for (int k = 0; k < 3; k++) {
    M[0][k] = R[0] * W[k] + R[1] * W[4 + k] + R[2] * W[8 + k];
    M[1][k] = U[0] * W[k] + U[1] * W[4 + k] + U[2] * W[8 + k];
    M[2][k] = F[0] * W[k] + F[1] * W[4 + k] + F[2] * W[8 + k];
  }
  /* a mirroring matrix (negative scale) flips the winding */
  float det = W[0] * (W[5] * W[10] - W[6] * W[9]) - W[1] * (W[4] * W[10] - W[6] * W[8]) + W[2] * (W[4] * W[9] - W[5] * W[8]);
  float ex = W[3] - cam->x, ey = W[7] - cam->y, ez = W[11] - cam->z;
  float o0 = R[0] * ex + R[1] * ey + R[2] * ez, o1 = U[0] * ex + U[1] * ey + U[2] * ez, o2 = F[0] * ex + F[1] * ey + F[2] * ez;
  float hx = (f->w * 0.5f) / cam->focal, hy = (f->h * 0.5f) / cam->focal;
  for (int i = 0; i < m->ntris; i++) {
    const LTri *tr = &m->tris[i]; CV cv[3]; float uv[6]; int oc = 0x1F;
    for (int k = 0; k < 3; k++) {
      const LVtx *v = &tr->v[k];
      float x = M[0][0] * v->x + M[0][1] * v->y + M[0][2] * v->z + o0;
      float y = M[1][0] * v->x + M[1][1] * v->y + M[1][2] * v->z + o1;
      float z = M[2][0] * v->x + M[2][1] * v->y + M[2][2] * v->z + o2;
      oc &= (x > z * hx) | (x < -z * hx) << 1 | (y > z * hy) << 2 | (y < -z * hy) << 3 | (z < NEAR_Z) << 4;
      cv[k].x = x; cv[k].y = y; cv[k].z = z;
    }
    if (oc) continue;                                   /* all corners outside one plane of the view */
    const RTex *rt = mw_rtex(mw, tr->mat); if (!rt) continue;
    for (int k = 0; k < 3; k++) {
      const LVtx *v = &tr->v[k];
      cv[k].r = v->r; cv[k].g = v->g; cv[k].b = v->b; uv[k * 2] = v->u; uv[k * 2 + 1] = v->v;
      cv[k].d = sqrtf(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
    }
    set_uv(cv, uv, rt);
    int va = rt->mode == RT_BLEND ? (tr->v[0].a + tr->v[1].a + tr->v[2].a) * 256 / 765 : 256;
    int cull = mw->mat[tr->mat].cull == 2;
    if (cull && det < 0) emit_tri_tex(f, cam, &cv[0], &cv[2], &cv[1], rt, va, 1, st);
    else emit_tri_tex(f, cam, &cv[0], &cv[1], &cv[2], rt, va, cull, st);
  }
}

void cam_basis(const Cam *cam, float R[3], float U[3], float F[3]) {
  float cy_ = cosf(cam->yaw), sy_ = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
  R[0] = cy_; R[1] = 0; R[2] = -sy_;
  U[0] = -sy_ * sp; U[1] = cp; U[2] = -cy_ * sp;
  F[0] = sy_ * cp; F[1] = sp; F[2] = cy_ * cp;
}

void render_mesh_xf(Frame *f, const Cam *cam, const LMesh *m, const float W[12], const uint32_t *atlas, RenderStats *st) {
  float R[3], U[3], F[3]; cam_basis(cam, R, U, F);
  /* camera-space M = rows(R,U,F) * W (rotation/scale part), offset = rows(R,U,F) * (W translation - camera) */
  float M[3][3];
  for (int k = 0; k < 3; k++) {
    M[0][k] = R[0] * W[k] + R[1] * W[4 + k] + R[2] * W[8 + k];
    M[1][k] = U[0] * W[k] + U[1] * W[4 + k] + U[2] * W[8 + k];
    M[2][k] = F[0] * W[k] + F[1] * W[4 + k] + F[2] * W[8 + k];
  }
  float ex = W[3] - cam->x, ey = W[7] - cam->y, ez = W[11] - cam->z;
  float o0 = R[0] * ex + R[1] * ey + R[2] * ez, o1 = U[0] * ex + U[1] * ey + U[2] * ez, o2 = F[0] * ex + F[1] * ey + F[2] * ez;
  for (int i = 0; i < m->ntris; i++) {
    const LTri *tr = &m->tris[i]; CV cv[3]; int behind = 0;
    for (int k = 0; k < 3; k++) {
      const LVtx *v = &tr->v[k];
      cv[k].x = M[0][0] * v->x + M[0][1] * v->y + M[0][2] * v->z + o0;
      cv[k].y = M[1][0] * v->x + M[1][1] * v->y + M[1][2] * v->z + o1;
      cv[k].z = M[2][0] * v->x + M[2][1] * v->y + M[2][2] * v->z + o2;
      cv[k].u = v->u; cv[k].v = v->v; cv[k].r = v->r; cv[k].g = v->g; cv[k].b = v->b;
      cv[k].d = sqrtf(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
      if (cv[k].z < NEAR_Z) behind++;
    }
    if (behind == 3) continue;
    emit_tri(f, cam, &cv[0], &cv[1], &cv[2], atlas && tr->v[0].u >= 0 ? atlas : NULL, 0, tr->alpha_blend, st);   /* u < 0: untextured */
  }
}

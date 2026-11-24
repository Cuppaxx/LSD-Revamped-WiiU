#include "render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR_Z 0.03f

#define MW_SLOTS 6
/* projection slots shared by the mesh worlds and the LBD levels: a Frame header per render thread whose triangle lists are
 * the slot's own (kept, with their bins, across frames), attached to the real frame afterwards (no copying) */
static Frame s_sf[MW_SLOTS]; static RenderStats s_st[MW_SLOTS]; static RList s_lists[MW_SLOTS][2];

typedef struct { float x, y, z, u, v, r, g, b, d; } CV;     /* camera-space vertex */

float g_snap_step = 1.0f;   /* vertex snapping grid in pixels (Frame height / 240, the PS1 grid at any resolution); 0 = off */
/* texture warping (Revamped GameSettings.AffineIntensity, default 0.5): q = (1 - 0.75 I) / z + I, see raster.c */
static float aff_a = 0.625f, aff_b = 0.5f;
void render_set_affine(float i) { if (i < 0) i = 0; if (i > 1) i = 1; aff_a = 1.0f - 0.75f * i; aff_b = i; }
/* camera space -> screen pixel (with the PS1 vertex snapping) */
static inline void proj_xy(float cxs, float cys, float iz, float focal, float cx, float cy, float *ox, float *oy) {
  float x = cx + focal * cxs * iz, y = cy - focal * cys * iz;
  if (g_snap_step >= 1.0f) {                     /* PS1-style vertex snapping (the wobble) */
    if (x < -1e8f || x > 1e8f || y < -1e8f || y > 1e8f) { *ox = x; *oy = y; return; }   /* (vertices at the near plane) */
    if (g_snap_step == 1.0f) { x = (float)ifloorf(x); y = (float)ifloorf(y); }
    else { float s = g_snap_step, is = 1.0f / s; x = (float)ifloorf(x * is) * s; y = (float)ifloorf(y * is) * s; }
  }
  *ox = x; *oy = y;
}
static inline void to_sv(const CV *c, float focal, SVtx *o, float cx, float cy) {
  float iz = 1.0f / c->z;
  o->q = aff_a * iz + aff_b;
  proj_xy(c->x, c->y, iz, focal, cx, cy, &o->x, &o->y);
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

/* Can the rasterizer draw any pixel of this screen triangle? It draws pixel centres only and drops |area| < 0.5, so a
 * triangle failing this test would cost a list entry and a setup per band for nothing (far views: most distant triangles
 * snap to a line or a dot). Exact: the same tests raster_tri makes (with a hair of margin on the area). */
static inline int tri_visible(const SVtx *a, const SVtx *b, const SVtx *c, int w, int h) {
  float cr = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
  if (cr > -0.499f && cr < 0.499f) return 0;
  float mnx = fmn(a->x, fmn(b->x, c->x)), mxx = fmx(a->x, fmx(b->x, c->x));
  float mny = fmn(a->y, fmn(b->y, c->y)), mxy = fmx(a->y, fmx(b->y, c->y));
  if (mxx < 0.0f || mxy < 0.0f || mnx > (float)w || mny > (float)h) return 0;   /* off screen */
  if (mxx - mnx > 2.0f && mxy - mny > 2.0f) return 1;  /* big enough to hold a pixel centre box-wise (the area test decides) */
  mnx = fmx(mnx, -1.0f); mny = fmx(mny, -1.0f); mxx = fmn(mxx, (float)w + 1.0f); mxy = fmn(mxy, (float)h + 1.0f);
  int x0 = iceilf(mnx - 0.5f), x1 = iceilf(mxx - 0.5f) - 1, y0 = iceilf(mny - 0.5f), y1 = iceilf(mxy - 0.5f) - 1;
  if (x0 > x1 || y0 > y1) return 0;                    /* no pixel centre inside the bounding box */
  if (x1 < 0 || x0 > w - 1 || y1 < 0 || y0 > h - 1) return 0;
  return 1;
}

static void emit_tri(Frame *f, const Cam *cam, const CV *a, const CV *b, const CV *c, const uint32_t *atlas, int water, int blend, RenderStats *st) {
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
  const WaterTex *wt = water && g_water && water <= g_water->n ? &g_water->t[water - 1] : NULL; const uint8_t *pal = wt ? wt->pal[g_texset & 3] : NULL;
  if (tri_visible(&s[0], &s[1], &s[2], f->w, f->h)) { raster_submit(f, &s[0], &s[1], &s[2], atlas, wt, pal, blend); st->tris_drawn++; } else st->tris_tiny++;
  if (n == 4) { if (tri_visible(&s[0], &s[2], &s[3], f->w, f->h)) { raster_submit(f, &s[0], &s[2], &s[3], atlas, wt, pal, blend); st->tris_drawn++; } else st->tris_tiny++; }
}

/* LBD tile meshes keep 3 own vertices per triangle; lmesh_index (level load) adds the unique positions and per-triangle
 * indices so render_chunk transforms and projects each corner once per tile (a ground quad: 4 instead of 6) */
#define PV_MAX 512
typedef struct { float x, y, z, sx, sy, iz, d; int in; } PV;
static PV s_pv[MW_SLOTS + 1][PV_MAX];
void lmesh_index(LMesh *m) {
  free(m->upos); free(m->uidx); m->upos = NULL; m->uidx = NULL; m->nupos = 0;
  if (m->ntris <= 0 || m->ntris * 3 > 4096) return;
  float *pos = (float *)malloc(sizeof(float) * 9 * (size_t)m->ntris); uint16_t *idx = (uint16_t *)malloc(sizeof(uint16_t) * 3 * (size_t)m->ntris);
  if (!pos || !idx) { free(pos); free(idx); return; }
  int n = 0;
  for (int i = 0; i < m->ntris; i++) for (int k = 0; k < 3; k++) {
    const LVtx *v = &m->tris[i].v[k]; int u = 0;
    while (u < n && !(pos[u * 3] == v->x && pos[u * 3 + 1] == v->y && pos[u * 3 + 2] == v->z)) u++;
    if (u == n) { pos[n * 3] = v->x; pos[n * 3 + 1] = v->y; pos[n * 3 + 2] = v->z; n++; }
    idx[i * 3 + k] = (uint16_t)u;
  }
  if (n > PV_MAX) { free(pos); free(idx); return; }
  m->upos = pos;                                       /* (allocated for 3 corners per triangle, n used) */
  m->uidx = idx; m->nupos = n;
}

void render_chunk(Frame *f, const Cam *cam, const LChunk *c, const LAtlas *atlas, float ox, float oz, RenderStats *st) {
  float R[3], U[3], F[3]; cam_basis(cam, R, U, F);
  float halfx = (f->w * 0.5f) / cam->focal + 0.1f;
  const float focal = cam->focal, hw = (float)(f->w / 2), hh = (float)(f->h / 2), dd2 = cam->draw_dist * cam->draw_dist;
  /* camera-space = rows(R,U,F) * Ry(dir) for the 4 tile directions (Unity: Ry columns x->(cs,0,-sn), y->(0,1,0), z->(sn,0,cs)) */
  float Md[4][3][3];
  for (int d = 0; d < 4; d++) {
    float cs = d == 0 ? 1.0f : d == 2 ? -1.0f : 0.0f, sn = d == 1 ? 1.0f : d == 3 ? -1.0f : 0.0f;
    float col[3][3] = {{cs, 0, -sn}, {0, 1, 0}, {sn, 0, cs}};
    for (int k = 0; k < 3; k++) {
      Md[d][0][k] = R[0] * col[k][0] + R[1] * col[k][1] + R[2] * col[k][2];
      Md[d][1][k] = U[0] * col[k][0] + U[1] * col[k][1] + U[2] * col[k][2];
      Md[d][2][k] = F[0] * col[k][0] + F[1] * col[k][1] + F[2] * col[k][2];
    }
  }
  int slot = (int)(f - s_sf); if (slot < 0 || slot >= MW_SLOTS) slot = MW_SLOTS;   /* the slot frames, or the main thread */
  PV *pv = s_pv[slot];
  int total = c->w * c->h;
  for (int idx = 0; idx < total; idx++) {                /* extra tiles are drawn as part of their parent's chain */
    int x = idx % c->w, y = idx / c->w;
    const LTile *chain[3]; int nchain = 0;
    const LTile *t = &c->tiles[idx];
    if (!t->draw) continue;
    chain[nchain++] = t;
    int guard = 0;
    while (t->extra >= 0 && t->extra < c->nextra && guard < 2) { t = &c->extra[t->extra]; chain[nchain++] = t; guard++; }
    for (int ci = 0; ci < nchain; ci++) {
      const LTile *tt = chain[ci];
      if (tt->type >= c->nmesh) continue;
      const LMesh *m = &c->mesh[tt->type];
      if (!m->ntris) continue;
      float wx = x + ox, wy = -(float)tt->height, wz = y + oz;
      /* cull by the tile's corner: behind, past the view distance, outside the horizontal view cone */
      float dx = wx - cam->x, dy = wy - cam->y, dz = wz - cam->z;
      float ccx = dx * R[0] + dy * R[1] + dz * R[2], ccz = dx * F[0] + dy * F[1] + dz * F[2];
      if (ccz < -1.8f) continue;
      if (dx * dx + dz * dz > dd2) continue;
      if (fabsf(ccx) > (ccz > 0 ? ccz : 0) * halfx + 2.2f) continue;
      st->tiles_drawn++;
      float (*M)[3] = Md[tt->dir & 3];
      float o0 = R[0] * dx + R[1] * dy + R[2] * dz, o1 = U[0] * dx + U[1] * dy + U[2] * dz, o2 = ccz;
      if (m->uidx) {                  /* indexed: each corner once */
        for (int u = 0; u < m->nupos; u++) {
          const float *p = m->upos + u * 3; PV *q = &pv[u];
          q->x = M[0][0] * p[0] + M[0][1] * p[1] + M[0][2] * p[2] + o0;
          q->y = M[1][0] * p[0] + M[1][1] * p[1] + M[1][2] * p[2] + o1;
          q->z = M[2][0] * p[0] + M[2][1] * p[1] + M[2][2] * p[2] + o2;
          q->in = q->z >= NEAR_Z; q->d = -1.0f;
          if (q->in) { q->iz = 1.0f / q->z; proj_xy(q->x, q->y, q->iz, focal, hw, hh, &q->sx, &q->sy); }
        }
        const uint16_t *ix = m->uidx;
        for (int i = 0; i < m->ntris; i++, ix += 3) {
          const LTri *tr = &m->tris[i];
          PV *p0 = &pv[ix[0]], *p1 = &pv[ix[1]], *p2 = &pv[ix[2]];
          int nin = p0->in + p1->in + p2->in;
          if (!nin) continue;
          const uint32_t *tex = tr->v[0].u >= 0 ? atlas->px : NULL;
          if (nin == 3) {
            SVtx s[3]; PV *pp[3] = {p0, p1, p2};
            for (int k = 0; k < 3; k++) { s[k].x = pp[k]->sx; s[k].y = pp[k]->sy; }
            if (!tri_visible(&s[0], &s[1], &s[2], f->w, f->h)) { st->tris_tiny++; continue; }
            for (int k = 0; k < 3; k++) {
              const LVtx *v = &tr->v[k]; PV *q = pp[k];
              if (q->d < 0) q->d = fsqrt(q->x * q->x + q->y * q->y + q->z * q->z);
              s[k].iz = q->iz; s[k].q = aff_a * q->iz + aff_b; s[k].u = v->u; s[k].v = v->v; s[k].r = v->r; s[k].g = v->g; s[k].b = v->b; s[k].d = q->d;
            }
            const WaterTex *wt = tr->water && g_water && tr->water <= g_water->n ? &g_water->t[tr->water - 1] : NULL;
            raster_submit(f, &s[0], &s[1], &s[2], tex, wt, wt ? wt->pal[g_texset & 3] : NULL, tr->alpha_blend); st->tris_drawn++;
            continue;
          }
          CV cv[3]; PV *pp[3] = {p0, p1, p2};                 /* crosses the near plane: clip */
          for (int k = 0; k < 3; k++) {
            const LVtx *v = &tr->v[k]; PV *q = pp[k];
            cv[k].x = q->x; cv[k].y = q->y; cv[k].z = q->z; cv[k].u = v->u; cv[k].v = v->v; cv[k].r = v->r; cv[k].g = v->g; cv[k].b = v->b;
            cv[k].d = fsqrt(q->x * q->x + q->y * q->y + q->z * q->z);
          }
          emit_tri(f, cam, &cv[0], &cv[1], &cv[2], tex, tr->water, tr->alpha_blend, st);
        }
        continue;
      }
      for (int i = 0; i < m->ntris; i++) {
        const LTri *tr = &m->tris[i];
        CV cv[3];
        int behind = 0;
        for (int k = 0; k < 3; k++) {                  /* positions first: most far triangles end at the pixel test */
          const LVtx *v = &tr->v[k];
          cv[k].x = M[0][0] * v->x + M[0][1] * v->y + M[0][2] * v->z + o0;
          cv[k].y = M[1][0] * v->x + M[1][1] * v->y + M[1][2] * v->z + o1;
          cv[k].z = M[2][0] * v->x + M[2][1] * v->y + M[2][2] * v->z + o2;
          if (cv[k].z < NEAR_Z) behind++;
        }
        if (behind == 3) continue;
        const uint32_t *tex = tr->v[0].u >= 0 ? atlas->px : NULL;
        if (!behind) {                                 /* nothing to clip: project, test, then the attributes */
          SVtx s[3];
          for (int k = 0; k < 3; k++) { s[k].iz = 1.0f / cv[k].z; proj_xy(cv[k].x, cv[k].y, s[k].iz, focal, hw, hh, &s[k].x, &s[k].y); }
          if (!tri_visible(&s[0], &s[1], &s[2], f->w, f->h)) { st->tris_tiny++; continue; }
          for (int k = 0; k < 3; k++) {
            const LVtx *v = &tr->v[k];
            s[k].q = aff_a * s[k].iz + aff_b; s[k].u = v->u; s[k].v = v->v; s[k].r = v->r; s[k].g = v->g; s[k].b = v->b;
            s[k].d = fsqrt(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
          }
          const WaterTex *wt = tr->water && g_water && tr->water <= g_water->n ? &g_water->t[tr->water - 1] : NULL;
          raster_submit(f, &s[0], &s[1], &s[2], tex, wt, wt ? wt->pal[g_texset & 3] : NULL, tr->alpha_blend); st->tris_drawn++;
          continue;
        }
        for (int k = 0; k < 3; k++) {
          const LVtx *v = &tr->v[k];
          cv[k].u = v->u; cv[k].v = v->v; cv[k].r = v->r; cv[k].g = v->g; cv[k].b = v->b;
          cv[k].d = fsqrt(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
        }
        emit_tri(f, cam, &cv[0], &cv[1], &cv[2], tex, tr->water, tr->alpha_blend, st);
      }
    }
  }
}

/* world box of everything render_chunk can draw of a chunk (main tiles and their extra chains, rotated like there) */
void chunk_bounds(const LChunk *c, float ox, float oz, float mn[3], float mx[3]) {
  mn[0] = mn[1] = mn[2] = 1e9f; mx[0] = mx[1] = mx[2] = -1e9f;
  for (int idx = 0; idx < c->w * c->h; idx++) {
    const LTile *t = &c->tiles[idx];
    if (!t->draw) continue;
    int x = idx % c->w, y = idx / c->w;
    for (int guard = 0; t && guard < 3; guard++) {
      if (t->type < c->nmesh) {
        const LMesh *m = &c->mesh[t->type]; float cs, sn;
        switch (t->dir & 3) { case 1: cs = 0; sn = 1; break; case 2: cs = -1; sn = 0; break; case 3: cs = 0; sn = -1; break; default: cs = 1; sn = 0; }
        for (int i = 0; i < m->ntris; i++) for (int k = 0; k < 3; k++) {
          const LVtx *v = &m->tris[i].v[k];
          float wx = x + ox + cs * v->x + sn * v->z, wy = -(float)t->height + v->y, wz = y + oz - sn * v->x + cs * v->z;
          if (wx < mn[0]) mn[0] = wx; if (wx > mx[0]) mx[0] = wx;
          if (wy < mn[1]) mn[1] = wy; if (wy > mx[1]) mx[1] = wy;
          if (wz < mn[2]) mn[2] = wz; if (wz > mx[2]) mx[2] = wz;
        }
      }
      t = t->extra >= 0 && t->extra < c->nextra ? &c->extra[t->extra] : NULL;
    }
  }
  if (mn[0] > mx[0]) { mn[0] = mn[1] = mn[2] = 0; mx[0] = mx[1] = mx[2] = 0; return; }
  for (int k = 0; k < 3; k++) { mn[k] -= 0.05f; mx[k] += 0.05f; }
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

void env_apply_fog(Frame *f, const Env *e, float scale, float far, int fade) {
  if (scale <= 0) scale = 1.0f;
  /* the lookup covers 0..64 tiles, or the (scaled) fog end / the view distance if further; 256 entries either way (the fog
   * has 8 coarse steps, so even 0..270 tiles is fine enough) */
  float range = fmx(64.0f, e->end * 1.05f);
  if (scale != 1.0f || fade) range = fmx(64.0f, far * 1.05f + 2.0f);
  f->fog_on = 1; f->fog_scale = 255.0f / range;
  float start = fmx(e->start * scale, 0.001f), end = fmx(e->end * scale, start + 0.001f);
  float f0 = far * 0.75f, fl = fmx(far - f0, 0.001f);
  for (int i = 0; i < 256; i++) {
    float d = i / f->fog_scale;
    float fog = (1.0f - start / fmx(d, start)) / (1.0f - start / end);
    float clear = (1.0f - sat(fog)); clear *= clear;
    clear = 1.0f - floorf((1.0f - clear) * 8.0f) / 8.0f;     /* classic: a few coarse steps */
    float inv = 1.0f - clear;
    float mul = e->sub ? clear : 1.0f, w = e->sub ? inv * inv : inv;  /* additive: c + fog*(1-clear); subtractive: c*clear + fog*(1-clear)^2 */
    float add[3]; for (int k = 0; k < 3; k++) add[k] = sat(e->fog[k] * w);
    if (fade && d > f0) {                                     /* far views: out = (c*mul + add)*(1-t) + horizon*t, same 8 steps */
      float t = floorf(sat((d - f0) / fl) * 8.0f) / 8.0f;
      mul *= 1.0f - t;
      for (int k = 0; k < 3; k++) add[k] = add[k] * (1.0f - t) + sat(e->skyfog[k]) * t;
    }
    f->fog_mul[i] = (uint16_t)(mul * 256.0f);
    for (int k = 0; k < 3; k++) f->fog_add[i][k] = (uint8_t)(add[k] * 255.0f);
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
  if (tri_visible(&s[0], &s[1], &s[2], f->w, f->h)) { raster_submit_tex(f, &s[0], &s[1], &s[2], rt, va); st->tris_drawn++; } else st->tris_tiny++;
  if (n == 4) { if (tri_visible(&s[0], &s[2], &s[3], f->w, f->h)) { raster_submit_tex(f, &s[0], &s[2], &s[3], rt, va); st->tris_drawn++; } else st->tris_tiny++; }
}

/* u,v (0..1, repeat) -> texels of the material's texture, shifted by whole repeats so the 16.16 rasterizer never overflows */
static inline void set_uv(CV *cv, const float *uv, const RTex *rt) {
  float u0 = (float)ifloorf(fmn(uv[0], fmn(uv[2], uv[4]))), v0 = (float)ifloorf(fmn(uv[1], fmn(uv[3], uv[5])));
  for (int k = 0; k < 3; k++) { cv[k].u = (uv[k * 2] - u0) * rt->w; cv[k].v = (uv[k * 2 + 1] - v0) * rt->h; }
}

float g_mw_detail = 1.0f;   /* reserved: fraction of the view distance for detail triangles */

/* the visible chunks are projected by all render threads at once (raster_parallel): each thread takes chunks one by one
 * and fills triangle lists of its own (a copy of the Frame header), which are appended to the frame's lists afterwards */
#define MW_LOD_D 24.0f      /* chunks further than this (past the classic view) draw their unsplit triangles */
typedef struct {
  Frame *f; const Cam *cam; const MeshWorld *mw;
  float R[3], U[3], F[3], hx, hy, far2;
  int *vis, nvis; int next; const float *key;   /* key: squared distance to the chunk box (far chunks: unsplit triangles) */
} MWJob;
static MWJob mwj;

static void slots_begin(const Frame *f) {
  for (int s = 0; s < MW_SLOTS; s++) {
    s_sf[s] = *f; s_sf[s].natt = 0;
    s_sf[s].lists[0] = s_lists[s][0]; s_sf[s].lists[1] = s_lists[s][1];
    rlist_clear(&s_sf[s].lists[0]); rlist_clear(&s_sf[s].lists[1]); memset(&s_st[s], 0, sizeof s_st[s]);
  }
}
static void slots_end(Frame *f, RenderStats *st) {
  for (int s = 0; s < MW_SLOTS; s++) {
    s_lists[s][0] = s_sf[s].lists[0]; s_lists[s][1] = s_sf[s].lists[1];   /* keep the grown buffers */
    if (s_lists[s][0].n || s_lists[s][1].n) raster_attach(f, &s_lists[s][0], &s_lists[s][1]);
    st->tris_drawn += s_st[s].tris_drawn; st->tris_tiny += s_st[s].tris_tiny; st->tiles_drawn += s_st[s].tiles_drawn;
  }
}

static void mw_chunk(MWJob *j, Frame *f, const MChunk *ch, int far, RenderStats *st) {
  const float *R = j->R, *U = j->U, *F = j->F; const float hx = j->hx, hy = j->hy, far2 = j->far2;
  const float ex = j->cam->x, ey = j->cam->y, ez = j->cam->z; const MeshWorld *mw = j->mw; const Cam *cam = j->cam;
  const float focal = cam->focal, hw = (float)(f->w / 2), hh = (float)(f->h / 2);
  {
    const MWTri *tris = far && ch->ftris ? ch->ftris : ch->tris; int ntri = far && ch->ftris ? ch->nfar : ch->ndrawn;
    for (int i = 0; i < ntri; i++) {
      const MWTri *t = &tris[i];
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
      int va = rt->mode == RT_BLEND ? (t->c[3] + t->c[7] + t->c[11]) * 256 / 765 : 256;
      if (cv[0].z >= NEAR_Z && cv[1].z >= NEAR_Z && cv[2].z >= NEAR_Z) {   /* nothing to clip: project and test before the attributes */
        SVtx s[3];
        for (int k = 0; k < 3; k++) { s[k].iz = 1.0f / cv[k].z; proj_xy(cv[k].x, cv[k].y, s[k].iz, focal, hw, hh, &s[k].x, &s[k].y); }
        if (m->cull == 2) { float cr = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y); if (cr <= 0.0f) continue; }
        if (!tri_visible(&s[0], &s[1], &s[2], f->w, f->h)) { st->tris_tiny++; continue; }
        set_uv(cv, t->uv, rt);
        for (int k = 0; k < 3; k++) {
          s[k].q = aff_a * s[k].iz + aff_b; s[k].u = cv[k].u; s[k].v = cv[k].v;
          s[k].r = t->c[k * 4]; s[k].g = t->c[k * 4 + 1]; s[k].b = t->c[k * 4 + 2]; s[k].d = fsqrt(cv[k].d);
        }
        raster_submit_tex(f, &s[0], &s[1], &s[2], rt, va); st->tris_drawn++;
        continue;
      }
      for (int k = 0; k < 3; k++) { cv[k].d = fsqrt(cv[k].d); cv[k].r = t->c[k * 4]; cv[k].g = t->c[k * 4 + 1]; cv[k].b = t->c[k * 4 + 2]; }
      set_uv(cv, t->uv, rt);
      emit_tri_tex(f, cam, &cv[0], &cv[1], &cv[2], rt, va, m->cull == 2, st);
    }
  }
}

static void mw_slot(int slot, void *ud) {
  MWJob *j = (MWJob *)ud; if (slot >= MW_SLOTS) return;
  Frame *sf = &s_sf[slot]; RenderStats *st = &s_st[slot];
  for (;;) {
    int k = __atomic_fetch_add(&j->next, 1, __ATOMIC_RELAXED);
    if (k >= j->nvis) break;
    mw_chunk(j, sf, &j->mw->chunks[j->vis[k]], j->key[k] > MW_LOD_D * MW_LOD_D, st);
  }
}

/* sort chunk indices by a key, nearest first (the biggest jobs start first, so the threads finish together) */
static void sort_by_key(int *idx, float *key, int n) {
  for (int i = 1; i < n; i++) { int v = idx[i]; float k = key[i]; int j = i - 1; while (j >= 0 && key[j] > k) { idx[j + 1] = idx[j]; key[j + 1] = key[j]; j--; } idx[j + 1] = v; key[j + 1] = k; }
}

/* box (min/max) against the view: 0 = outside the distance or one side/near plane of the frustum. P = 5 planes through
 * the eye with outward normals (inside <=> dot(n, p - eye) <= 0) */
static void view_planes(const float R[3], const float U[3], const float F[3], float hx, float hy, float P[5][3]) {
  for (int k = 0; k < 3; k++) {
    P[0][k] = R[k] - hx * F[k]; P[1][k] = -R[k] - hx * F[k]; P[2][k] = U[k] - hy * F[k]; P[3][k] = -U[k] - hy * F[k]; P[4][k] = -F[k];
  }
}
static int box_visible(const float mn[3], const float mx[3], const float eye[3], float far2, const float P[5][3], float *d2out) {
  float dx = fmx(fmx(mn[0] - eye[0], eye[0] - mx[0]), 0.0f), dy = fmx(fmx(mn[1] - eye[1], eye[1] - mx[1]), 0.0f), dz = fmx(fmx(mn[2] - eye[2], eye[2] - mx[2]), 0.0f);
  float d2 = dx * dx + dy * dy + dz * dz; if (d2out) *d2out = d2;
  if (d2 > far2) return 0;
  for (int p = 0; p < 5; p++) {
    float vx = P[p][0] > 0 ? mn[0] : mx[0], vy = P[p][1] > 0 ? mn[1] : mx[1], vz = P[p][2] > 0 ? mn[2] : mx[2];
    if (P[p][0] * (vx - eye[0]) + P[p][1] * (vy - eye[1]) + P[p][2] * (vz - eye[2]) > 0.05f) return 0;
  }
  return 1;
}

void render_meshworld(Frame *f, const Cam *cam, const MeshWorld *mw, RenderStats *st) {
  if (!mw || !mw->loaded) return;
  MWJob *j = &mwj;
  j->f = f; j->cam = cam; j->mw = mw;
  float *R = j->R, *U = j->U, *F = j->F; cam_basis(cam, R, U, F);
  j->hx = (f->w * 0.5f) / cam->focal; j->hy = (f->h * 0.5f) / cam->focal;
  float far = cam->draw_dist; j->far2 = far * far;
  float P[5][3]; view_planes(R, U, F, j->hx, j->hy, P);
  const float eye[3] = { cam->x, cam->y, cam->z };
  static int *vis; static float *key; static int viscap;
  if (viscap < mw->nchunks) { viscap = mw->nchunks; vis = (int *)realloc(vis, sizeof(int) * (size_t)viscap); key = (float *)realloc(key, sizeof(float) * (size_t)viscap); }
  if (!vis || !key) { viscap = 0; return; }
  j->vis = vis; j->nvis = 0; j->next = 0;
  for (int ci = 0; ci < mw->nchunks; ci++) {
    const MChunk *ch = &mw->chunks[ci];
    if (!ch->ndrawn) continue;
    /* the chunk's box against the view distance and the frustum, then per triangle in mw_chunk */
    float d2; if (!box_visible(ch->mn, ch->mx, eye, j->far2, P, &d2)) continue;
    key[j->nvis] = d2; vis[j->nvis++] = ci;
  }
  sort_by_key(vis, key, j->nvis); j->key = key;
  st->tiles_drawn += j->nvis;
  slots_begin(f);
  if (j->nvis > 1) raster_parallel(mw_slot, j); else mw_slot(0, j);
  slots_end(f, st);
}

/* ---- LBD levels: chunk boxes against the view, then the visible chunks projected on all render threads (render_chunk) */
typedef struct { const Cam *cam; const LChunk *ch; const float *ox, *oz; const LAtlas *atlas; int *vis, nvis, next; } LJob;
static void lbd_slot(int slot, void *ud) {
  LJob *j = (LJob *)ud; if (slot >= MW_SLOTS) return;
  for (;;) {
    int k = __atomic_fetch_add(&j->next, 1, __ATOMIC_RELAXED);
    if (k >= j->nvis) break;
    int i = j->vis[k];
    render_chunk(&s_sf[slot], j->cam, &j->ch[i], j->atlas, j->ox[i], j->oz[i], &s_st[slot]);
  }
}
void render_chunks(Frame *f, const Cam *cam, const LChunk *chunks, const float *ox, const float *oz, const float (*box)[6], int n, const LAtlas *atlas, RenderStats *st) {
  float R[3], U[3], F[3]; cam_basis(cam, R, U, F);
  float hx = (f->w * 0.5f) / cam->focal, hy = (f->h * 0.5f) / cam->focal, P[5][3]; view_planes(R, U, F, hx, hy, P);
  const float eye[3] = { cam->x, cam->y, cam->z }; float far = cam->draw_dist + 1.5f;   /* tiles are culled by their corner */
  static int *vis; static float *key; static int viscap;
  if (viscap < n) { viscap = n; vis = (int *)realloc(vis, sizeof(int) * (size_t)viscap); key = (float *)realloc(key, sizeof(float) * (size_t)viscap); }
  if (!vis || !key) { viscap = 0; return; }
  LJob j = { cam, chunks, ox, oz, atlas, vis, 0, 0 };
  for (int i = 0; i < n; i++) {
    float d2;
    if (box) { if (!box_visible(box[i], box[i] + 3, eye, far * far, P, &d2)) continue; }
    else { float dx = ox[i] + 10 - cam->x, dz = oz[i] + 10 - cam->z; d2 = dx * dx + dz * dz; if (d2 > (cam->draw_dist + 15) * (cam->draw_dist + 15)) continue; }
    key[j.nvis] = d2; vis[j.nvis++] = i;
  }
  sort_by_key(vis, key, j.nvis);
  slots_begin(f);
  if (j.nvis > 1) raster_parallel(lbd_slot, &j); else lbd_slot(0, &j);
  slots_end(f, st);
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
      cv[k].d = fsqrt(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
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
      cv[k].d = fsqrt(cv[k].x * cv[k].x + cv[k].y * cv[k].y + cv[k].z * cv[k].z);
      if (cv[k].z < NEAR_Z) behind++;
    }
    if (behind == 3) continue;
    emit_tri(f, cam, &cv[0], &cv[1], &cv[2], atlas && tr->v[0].u >= 0 ? atlas : NULL, 0, tr->alpha_blend, st);   /* u < 0: untextured */
  }
}

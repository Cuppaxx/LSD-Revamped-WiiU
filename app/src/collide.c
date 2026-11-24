#include "collide.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void build_grid(CWorld *w);

static void add_tri(CTri **arr, int *n, int *cap, const float *a, const float *b, const float *c, int tag) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 1024; *arr = (CTri *)realloc(*arr, (size_t)*cap * sizeof(CTri)); }
  CTri *t = &(*arr)[(*n)++];
  memcpy(t->a, a, 12); memcpy(t->b, b, 12); memcpy(t->c, c, 12); t->tag = tag;
  float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
  float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
  float l = sqrtf(nx * nx + ny * ny + nz * nz);
  if (l < 1e-9f) { (*n)--; return; }                  /* degenerate */
  t->n[0] = nx / l; t->n[1] = ny / l; t->n[2] = nz / l;
  t->minx = fminf(a[0], fminf(b[0], c[0])); t->maxx = fmaxf(a[0], fmaxf(b[0], c[0]));
  t->minz = fminf(a[2], fminf(b[2], c[2])); t->maxz = fmaxf(a[2], fmaxf(b[2], c[2]));
  t->miny = fminf(a[1], fminf(b[1], c[1])); t->maxy = fmaxf(a[1], fmaxf(b[1], c[1]));
}

static void add_tile_mesh(CTri **arr, int *n, int *cap, const LMesh *m, const LTile *t, float wx, float wz, int tag) {
  float cs, sn;
  switch (t->dir & 3) { case 1: cs = 0; sn = 1; break; case 2: cs = -1; sn = 0; break; case 3: cs = 0; sn = -1; break; default: cs = 1; sn = 0; }
  float wy = -(float)t->height;
  for (int i = 0; i < m->ntris; i++) {
    float p[3][3];
    for (int k = 0; k < 3; k++) {
      const LVtx *v = &m->tris[i].v[k];
      p[k][0] = v->x * cs + v->z * sn + wx; p[k][1] = v->y + wy; p[k][2] = -v->x * sn + v->z * cs + wz;
    }
    add_tri(arr, n, cap, p[0], p[1], p[2], tag);
  }
}

void cw_build(CWorld *w, const LChunk *chunks, const float *ox, const float *oz, int nchunks) {
  memset(w, 0, sizeof *w);
  int cap = 0;
  for (int ci = 0; ci < nchunks; ci++) {
    const LChunk *c = &chunks[ci];
    int total = c->w * c->h;
    for (int idx = 0; idx < total; idx++) {
      const LTile *t = &c->tiles[idx];
      if (!t->draw) continue;
      float wx = idx % c->w + ox[ci], wz = idx / c->w + oz[ci];
      const LTile *cur = t; int guard = 0;
      for (;;) {
        if (cur->type < c->nmesh) add_tile_mesh(&w->tris, &w->ntris, &cap, &c->mesh[cur->type], cur, wx, wz, (ci << 16) | idx);
        if (cur->extra < 0 || cur->extra >= c->nextra || guard++ >= 2) break;
        cur = &c->extra[cur->extra];
      }
    }
  }
  build_grid(w);
}

void cw_build_tris(CWorld *w, const float *xyz, int n) {
  memset(w, 0, sizeof *w);
  int cap = n + 1;
  w->tris = (CTri *)malloc((size_t)cap * sizeof(CTri));
  if (!w->tris) cap = 0;
  for (int i = 0; i < n; i++) add_tri(&w->tris, &w->ntris, &cap, xyz + i * 9, xyz + i * 9 + 3, xyz + i * 9 + 6, i);
  build_grid(w);
}

static void build_grid(CWorld *w) {
  float mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f; w->miny = 1e9f;
  for (int i = 0; i < w->ntris; i++) {
    const CTri *t = &w->tris[i];
    mnx = fminf(mnx, t->minx); mxx = fmaxf(mxx, t->maxx); mnz = fminf(mnz, t->minz); mxz = fmaxf(mxz, t->maxz); w->miny = fminf(w->miny, t->miny);
  }
  if (!w->ntris) { mnx = mxx = mnz = mxz = 0; w->miny = 0; }
  w->x0 = floorf(mnx) - 1; w->z0 = floorf(mnz) - 1;
  w->gw = (int)(floorf(mxx) - w->x0) + 2; w->gh = (int)(floorf(mxz) - w->z0) + 2;
  int ncell = w->gw * w->gh;
  w->start = (int *)calloc((size_t)ncell + 1, sizeof(int));
  for (int pass = 0; pass < 2; pass++) {
    int *fill = pass ? (int *)calloc((size_t)ncell, sizeof(int)) : NULL;
    if (pass) { for (int i = 0, s = 0; i <= ncell; i++) { int cnt = w->start[i]; w->start[i] = s; s += cnt; } w->idx = (int *)malloc((size_t)w->start[ncell] * sizeof(int) + 4); }
    for (int i = 0; i < w->ntris; i++) {
      const CTri *t = &w->tris[i];
      int cx0 = (int)floorf(t->minx - w->x0), cx1 = (int)floorf(t->maxx - w->x0), cz0 = (int)floorf(t->minz - w->z0), cz1 = (int)floorf(t->maxz - w->z0);
      for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
        if (cx < 0 || cz < 0 || cx >= w->gw || cz >= w->gh) continue;
        int cell = cz * w->gw + cx;
        if (!pass) w->start[cell]++; else w->idx[w->start[cell] + fill[cell]++] = i;
      }
    }
    free(fill);
  }
}

void cw_free(CWorld *w) { free(w->tris); free(w->start); free(w->idx); free(w->dyn); memset(w, 0, sizeof *w); }

void cw_add_dyn(CWorld *w, const CDyn *d) {
  CDyn *n = (CDyn *)realloc(w->dyn, (size_t)(w->ndyn + 1) * sizeof(CDyn)); if (!n) return;
  w->dyn = n; w->dyn[w->ndyn++] = *d;
}

static int dyn_blocked(const CWorld *w, float x, float z, float y0, float y1, float r) {
  for (int i = 0; i < w->ndyn; i++) {
    const CDyn *d = &w->dyn[i];
    if (w->live && d->ent >= 0 && !w->live[d->ent]) continue;     /* the entity (or a parent) is inactive */
    if (d->y1 < y0 || d->y0 > y1) continue;
    float dx = x - d->x, dz = z - d->z;
    if (d->type == 1) { float rr = d->r + r; if (dx * dx + dz * dz < rr * rr) return 1; }
    else {                                                            /* oriented box: into its frame (inverse yaw) */
      float lx = dx * d->cs - dz * d->sn, lz = dx * d->sn + dz * d->cs;
      if (fabsf(lx) < d->hx + r && fabsf(lz) < d->hz + r) return 1;
    }
  }
  return 0;
}

static int in_tri_xz(const CTri *t, float x, float z, float eps) {
  float d0 = (t->b[0] - t->a[0]) * (z - t->a[2]) - (t->b[2] - t->a[2]) * (x - t->a[0]);
  float d1 = (t->c[0] - t->b[0]) * (z - t->b[2]) - (t->c[2] - t->b[2]) * (x - t->b[0]);
  float d2 = (t->a[0] - t->c[0]) * (z - t->c[2]) - (t->a[2] - t->c[2]) * (x - t->c[0]);
  int neg = d0 < -eps || d1 < -eps || d2 < -eps, pos = d0 > eps || d1 > eps || d2 > eps;
  return !(neg && pos);
}

int cw_ground(const CWorld *w, float x, float z, float ymax, float *gy) { int t; return cw_ground_ex(w, x, z, ymax, gy, &t); }
int cw_ground_ex(const CWorld *w, float x, float z, float ymax, float *gy, int *tri) {
  *tri = -1;
  int cx = (int)floorf(x - w->x0), cz = (int)floorf(z - w->z0);
  if (cx < 0 || cz < 0 || cx >= w->gw || cz >= w->gh) return 0;
  int cell = cz * w->gw + cx, found = 0; float best = -1e9f;
  for (int k = w->start[cell]; k < w->start[cell + 1]; k++) {
    const CTri *t = &w->tris[w->idx[k]];
    float ny = t->n[1];
    if (fabsf(ny) < SLOPE_COS) continue;
    if (x < t->minx - 1e-4f || x > t->maxx + 1e-4f || z < t->minz - 1e-4f || z > t->maxz + 1e-4f) continue;
    if (t->miny > ymax) continue;
    if (!in_tri_xz(t, x, z, 1e-6f)) continue;
    float y = t->a[1] - (t->n[0] * (x - t->a[0]) + t->n[2] * (z - t->a[2])) / ny;
    if (y <= ymax && y > best) { best = y; found = 1; *tri = w->idx[k]; }
  }
  if (found) *gy = best;
  return found;
}

static float seg_d2(float px, float pz, float ax, float az, float bx, float bz, float *tt) {
  float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  float t = l2 > 1e-12f ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
  if (t < 0) t = 0; if (t > 1) t = 1;
  float cx = ax + dx * t - px, cz = az + dz * t - pz;
  *tt = t; return cx * cx + cz * cz;
}

int cw_blocked(const CWorld *w, float x, float z, float y0, float y1, float r) {
  int cx0 = (int)floorf(x - r - w->x0), cx1 = (int)floorf(x + r - w->x0), cz0 = (int)floorf(z - r - w->z0), cz1 = (int)floorf(z + r - w->z0);
  for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
    if (cx < 0 || cz < 0 || cx >= w->gw || cz >= w->gh) continue;
    int cell = cz * w->gw + cx;
    for (int k = w->start[cell]; k < w->start[cell + 1]; k++) {
      const CTri *t = &w->tris[w->idx[k]];
      float ny = fabsf(t->n[1]);
      if (ny >= SLOPE_COS) continue;                                   /* walkable floor/ceiling */
      if (t->maxy < y0 || t->miny > y1) continue;
      if (x + r < t->minx || x - r > t->maxx || z + r < t->minz || z - r > t->maxz) continue;
      if (ny < 0.08f) {                                                /* (near) vertical wall: its footprint is its projected edges */
        float tt;
        if (seg_d2(x, z, t->a[0], t->a[2], t->b[0], t->b[2], &tt) < r * r) return 1;
        if (seg_d2(x, z, t->b[0], t->b[2], t->c[0], t->c[2], &tt) < r * r) return 1;
        if (seg_d2(x, z, t->c[0], t->c[2], t->a[0], t->a[2], &tt) < r * r) return 1;
      } else {                                                         /* steep slope: blocked where its surface is inside the body */
        if (in_tri_xz(t, x, z, 1e-6f)) {
          float h = t->a[1] - (t->n[0] * (x - t->a[0]) + t->n[2] * (z - t->a[2])) / t->n[1];
          if (h >= y0 - r && h <= y1 + r) return 1;
        }
      }
    }
  }
  return w->ndyn ? dyn_blocked(w, x, z, y0, y1, r) : 0;
}

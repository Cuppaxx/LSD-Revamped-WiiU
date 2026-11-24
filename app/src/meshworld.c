/* meshworld.c - see meshworld.h. world.bin (big-endian, tools/ADDL_FORMAT.md section 3):
 *   "AWLD" u32 version(1) f32 cell
 *   u16 ntex, ntex x (str file, u16 w, u16 h)
 *   u16 nmat, nmat x (u8 shader, u8 cull, i16 tex[4], i16 pal[4], f32 alpha, f32 speed, str name)
 *   f32 bbox_min[3], bbox_max[3]
 *   u32 nchunks, nchunks x (i16 cx, cz, f32 min[3], max[3], u32 ntris, ntris x tri)
 *   tri (76 bytes): u16 mat, u8 flags (1 drawn, 2 solid), u8 0, 3 x (f32 x, y, z, f32 u, v, u8 r, g, b, a)
 * Textures (ADDTEX/NAME.tex): "ATEX" u16 w, u16 h, w*h RGBA8 rows top to bottom. Kept in a global cache across dream loads
 * (the four 512x512 atlases are shared by all 14 dreams). */
#include "meshworld.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *p, *end; int bad; } Rd;
static int need(Rd *r, size_t n) { if (r->bad || (size_t)(r->end - r->p) < n) { r->bad = 1; return 0; } return 1; }
static unsigned u8_(Rd *r) { if (!need(r, 1)) return 0; return *r->p++; }
static unsigned u16_(Rd *r) { if (!need(r, 2)) return 0; unsigned v = (unsigned)r->p[0] << 8 | r->p[1]; r->p += 2; return v; }
static uint32_t u32_(Rd *r) { if (!need(r, 4)) return 0; uint32_t v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16 | (uint32_t)r->p[2] << 8 | r->p[3]; r->p += 4; return v; }
static float f32_(Rd *r) { uint32_t v = u32_(r); float f; memcpy(&f, &v, 4); return isfinite(f) ? f : 0.0f; }
static void str_(Rd *r, char *out, size_t cap) {
  unsigned n = u8_(r); if (!need(r, n)) { out[0] = 0; return; }
  size_t k = n < cap - 1 ? n : cap - 1; memcpy(out, r->p, k); out[k] = 0; r->p += n;
}

/* ---------------------------------------------------------------- texture cache */
#define MAX_TEX 64
static MTexture g_tex[MAX_TEX]; static int g_ntex;
static uint32_t white_px = 0xFFFFFFFFu;

static int pow2_floor(int v) { int p = 1; while (p * 2 <= v) p *= 2; return p; }

static MTexture *tex_get(const char *name) {
  for (int i = 0; i < g_ntex; i++) if (!strcmp(g_tex[i].name, name)) return &g_tex[i];
  if (g_ntex == MAX_TEX) return NULL;
  MTexture *t = &g_tex[g_ntex++]; memset(t, 0, sizeof *t); snprintf(t->name, sizeof t->name, "%s", name);
  return t;
}
static void tex_load(MTexture *t) {
  if (t->px) return;
  size_t n; uint8_t *b = lsd_read_file(t->name, &n);
  if (!b) { lsd_log("missing texture %s", t->name); return; }
  if (n < 8 || memcmp(b, "ATEX", 4)) { lsd_log("bad texture %s", t->name); free(b); return; }
  int w = b[4] << 8 | b[5], h = b[6] << 8 | b[7];
  if (w <= 0 || h <= 0 || n < 8 + (size_t)w * h * 4) { lsd_log("short texture %s", t->name); free(b); return; }
  int pw = pow2_floor(w), ph = pow2_floor(h);            /* the rasterizer wraps with masks: resample odd sizes (none in the mod) */
  t->px = (uint32_t *)malloc((size_t)pw * ph * 4);
  if (t->px) {
    for (int y = 0; y < ph; y++) for (int x = 0; x < pw; x++) {
      const uint8_t *s = b + 8 + ((size_t)(y * h / ph) * w + (size_t)(x * w / pw)) * 4;
      t->px[y * pw + x] = (uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
    }
    t->w = pw; t->h = ph;
  }
  free(b);
}

static int log2i(int v) { int s = 0; while ((1 << s) < v) s++; return s; }
static void rtex_from(RTex *rt, const MTexture *t) {
  if (t && t->px) { rt->px = t->px; rt->w = t->w; rt->h = t->h; }
  else { rt->px = &white_px; rt->w = rt->h = 1; }
  rt->wmask = (uint32_t)rt->w - 1; rt->hmask = (uint32_t)rt->h - 1; rt->wshift = log2i(rt->w);
}

/* ---------------------------------------------------------------- world.bin */
void mw_free(MeshWorld *mw) { free(mw->tex); free(mw->mat); free(mw->chunks); free(mw->tris); free(mw->ftris); memset(mw, 0, sizeof *mw); }

static void read_tri(Rd *r, MWTri *t) {
  t->mat = (uint16_t)u16_(r); t->flags = (uint8_t)u8_(r); u8_(r); t->pad = 0;
  for (int k = 0; k < 3; k++) {
    t->p[k * 3] = f32_(r); t->p[k * 3 + 1] = f32_(r); t->p[k * 3 + 2] = f32_(r);
    t->uv[k * 2] = f32_(r); t->uv[k * 2 + 1] = f32_(r);
    for (int c = 0; c < 4; c++) t->c[k * 4 + c] = (uint8_t)u8_(r);
  }
}

/* Big triangles (room floors and walls of a few units) look badly warped with the rasterizer's affine (PS1) texturing when
 * the camera is close, and get all-or-nothing fog: drawn triangles with an edge longer than MAX_EDGE are split at the middle
 * of their longest edge (winding kept) for drawing; the original stays as a collision-only triangle. */
#define MAX_EDGE 1.25f
typedef struct { MWTri *t; int n, cap; } TriBuf;
static void push_tri(TriBuf *b, const MWTri *t) {
  if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 4096; b->t = (MWTri *)realloc(b->t, sizeof(MWTri) * (size_t)b->cap); }
  b->t[b->n++] = *t;
}
static void copy_vtx(MWTri *o, int k, const MWTri *t, int s) {
  memcpy(o->p + k * 3, t->p + s * 3, 12); memcpy(o->uv + k * 2, t->uv + s * 2, 8); memcpy(o->c + k * 4, t->c + s * 4, 4);
}
static void mid_vtx(MWTri *o, int k, const MWTri *t, int a, int b) {
  for (int i = 0; i < 3; i++) o->p[k * 3 + i] = (t->p[a * 3 + i] + t->p[b * 3 + i]) * 0.5f;
  for (int i = 0; i < 2; i++) o->uv[k * 2 + i] = (t->uv[a * 2 + i] + t->uv[b * 2 + i]) * 0.5f;
  for (int i = 0; i < 4; i++) o->c[k * 4 + i] = (uint8_t)((t->c[a * 4 + i] + t->c[b * 4 + i] + 1) / 2);
}
static void split_push(TriBuf *b, const MWTri *t, int depth) {
  float best = 0; int e = 0;
  for (int i = 0; i < 3; i++) {
    int j = (i + 1) % 3; float d = 0;
    for (int k = 0; k < 3; k++) { float x = t->p[j * 3 + k] - t->p[i * 3 + k]; d += x * x; }
    if (d > best) { best = d; e = i; }
  }
  if (best <= MAX_EDGE * MAX_EDGE || depth >= 10) { push_tri(b, t); return; }
  int a = e, bb = (e + 1) % 3, c = (e + 2) % 3;
  MWTri t1 = *t, t2 = *t;
  copy_vtx(&t1, 0, t, a); mid_vtx(&t1, 1, t, a, bb); copy_vtx(&t1, 2, t, c);
  mid_vtx(&t2, 0, t, a, bb); copy_vtx(&t2, 1, t, bb); copy_vtx(&t2, 2, t, c);
  split_push(b, &t1, depth + 1); split_push(b, &t2, depth + 1);
}
static void subdivide(MeshWorld *mw) {
  TriBuf b = { 0, 0, 0 }, far = { 0, 0, 0 }; int *start = (int *)malloc(sizeof(int) * ((size_t)mw->nchunks + 1)), nsplit = 0;
  int *fstart = (int *)malloc(sizeof(int) * ((size_t)mw->nchunks + 1)), *fn = (int *)calloc((size_t)mw->nchunks + 1, sizeof(int));
  if (!start || !fstart || !fn) { free(start); free(fstart); free(fn); return; }
  for (int c = 0; c < mw->nchunks; c++) {
    MChunk *ch = &mw->chunks[c]; start[c] = b.n; int split0 = nsplit; fstart[c] = far.n;
    for (int i = 0; i < ch->ntris; i++) if (ch->tris[i].flags & 1) { MWTri d = ch->tris[i]; d.flags = 1; push_tri(&far, &d); }
    for (int i = 0; i < ch->ntris; i++) {
      MWTri t = ch->tris[i]; int n0 = b.n;
      if (!(t.flags & 1)) { push_tri(&b, &t); continue; }
      MWTri d = t; d.flags = 1; split_push(&b, &d, 0);
      if (b.n - n0 == 1) b.t[b.n - 1].flags = t.flags;          /* not split: the triangle itself */
      else { nsplit++; if (t.flags & 2) { t.flags = 2; push_tri(&b, &t); } }
    }
    if (nsplit == split0) far.n = fstart[c]; else fn[c] = far.n - fstart[c];   /* keep the unsplit copy only where it differs */
  }
  start[mw->nchunks] = b.n;
  if (!nsplit) { free(b.t); free(far.t); free(start); free(fstart); free(fn); return; }
  free(mw->tris); mw->tris = b.t; mw->ntris = b.n;
  for (int c = 0; c < mw->nchunks; c++) { mw->chunks[c].tris = b.t + start[c]; mw->chunks[c].ntris = start[c + 1] - start[c]; }
  mw->ftris = far.t; mw->nfar = far.n;
  for (int c = 0; c < mw->nchunks; c++) if (fn[c]) { mw->chunks[c].ftris = far.t + fstart[c]; mw->chunks[c].nfar = fn[c]; }
  free(start); free(fstart); free(fn);
  lsd_log("mesh world: %d big triangles split for drawing -> %d triangles (%d unsplit kept for far chunks)", nsplit, mw->ntris, mw->nfar);
}

int mw_load(MeshWorld *mw, const char *path) {
  mw_free(mw);
  size_t n; uint8_t *buf = lsd_read_file(path, &n);
  if (!buf) { lsd_log("missing %s", path); return -1; }
  Rd r = { buf, buf + n, 0 };
  if (n < 12 || memcmp(buf, "AWLD", 4)) { free(buf); return -2; }
  r.p += 4;
  uint32_t ver = u32_(&r); if (ver != 1) lsd_log("%s: version %u, expected 1", path, (unsigned)ver);
  mw->cell = f32_(&r);
  mw->ntex = (int)u16_(&r);
  mw->tex = (MTexture **)calloc((size_t)mw->ntex + 1, sizeof(MTexture *));
  for (int i = 0; i < mw->ntex; i++) { char nm[64]; str_(&r, nm, sizeof nm); u16_(&r); u16_(&r); mw->tex[i] = tex_get(nm); }
  mw->nmat = (int)u16_(&r);
  mw->mat = (MMat *)calloc((size_t)mw->nmat + 1, sizeof(MMat));
  for (int i = 0; i < mw->nmat; i++) {
    MMat *m = &mw->mat[i];
    m->shader = (uint8_t)u8_(&r); m->cull = (uint8_t)u8_(&r);
    for (int k = 0; k < 4; k++) m->tex[k] = (int16_t)u16_(&r);
    for (int k = 0; k < 4; k++) m->pal[k] = (int16_t)u16_(&r);
    m->alpha = f32_(&r); m->speed = f32_(&r); str_(&r, m->name, sizeof m->name);
  }
  for (int k = 0; k < 3; k++) mw->bmin[k] = f32_(&r);
  for (int k = 0; k < 3; k++) mw->bmax[k] = f32_(&r);
  uint32_t nc = u32_(&r);
  if (nc > 65536) r.bad = 1;
  /* two passes: count the triangles to allocate one array, then read */
  const uint8_t *chunks_at = r.p; uint32_t total = 0;
  for (uint32_t c = 0; c < nc && !r.bad; c++) { if (!need(&r, 28)) break; r.p += 28; uint32_t nt = u32_(&r); if (!need(&r, (size_t)nt * 76)) break; r.p += (size_t)nt * 76; total += nt; }
  if (r.bad) { lsd_log("%s: corrupt", path); free(buf); mw_free(mw); return -3; }
  r.p = chunks_at;
  mw->chunks = (MChunk *)calloc((size_t)nc + 1, sizeof(MChunk)); mw->nchunks = (int)nc;
  mw->tris = (MWTri *)malloc(sizeof(MWTri) * ((size_t)total + 1)); mw->ntris = (int)total;
  if (!mw->chunks || !mw->tris) { free(buf); mw_free(mw); return -4; }
  MWTri *tp = mw->tris;
  for (uint32_t c = 0; c < nc; c++) {
    MChunk *ch = &mw->chunks[c];
    ch->cx = (int16_t)u16_(&r); ch->cz = (int16_t)u16_(&r);
    for (int k = 0; k < 3; k++) ch->mn[k] = f32_(&r);
    for (int k = 0; k < 3; k++) ch->mx[k] = f32_(&r);
    ch->ntris = (int)u32_(&r); ch->tris = tp;
    for (int i = 0; i < ch->ntris; i++) { read_tri(&r, &tp[i]); if (tp[i].mat >= mw->nmat) tp[i].mat = 0; }
    tp += ch->ntris;
  }
  free(buf);
  subdivide(mw);
  for (int c = 0; c < mw->nchunks; c++) {
    MChunk *ch = &mw->chunks[c]; tp = ch->tris;
    /* drawn triangles first (the renderer stops at ndrawn), collider-only ones after */
    int a = 0; for (int i = 0; i < ch->ntris; i++) if (tp[i].flags & 1) { if (i != a) { MWTri s = tp[a]; tp[a] = tp[i]; tp[i] = s; } a++; }
    ch->ndrawn = a; mw->ndrawn += a;
    for (int i = 0; i < ch->ntris; i++) mw->nsolid += (tp[i].flags & 2) != 0;
  }
  mw->loaded = 1; mw->texset = -1;
  return 0;
}

void mw_set_texset(MeshWorld *mw, int set) {
  set &= 3; mw->texset = set;
  for (int i = 0; i < mw->nmat; i++) {
    MMat *m = &mw->mat[i]; RTex *rt = &m->rt; memset(rt, 0, sizeof *rt);
    int ti = m->tex[set]; if (ti < 0) ti = m->tex[0];      /* only set A of the water palette exists in the mod */
    MTexture *t = ti >= 0 && ti < mw->ntex ? mw->tex[ti] : NULL;
    if (t) tex_load(t);
    rtex_from(rt, t);
    rt->mode = m->shader == 1 ? RT_BLEND : m->shader == 2 ? RT_WATER : RT_CUTOUT;
    if (rt->mode == RT_WATER) {
      int pi = m->pal[set]; if (pi < 0) pi = m->pal[0];
      MTexture *p = pi >= 0 && pi < mw->ntex ? mw->tex[pi] : NULL;
      if (p) tex_load(p);
      if (!p || !p->px || !t || !t->px) rt->mode = RT_BLEND;   /* broken water: plain half-transparent */
      else { rt->pal = p->px; rt->palw = p->w; }
      rt->walpha = (int)(m->alpha * 256.0f); rt->speed = m->speed;
    }
  }
}

const RTex *mw_rtex(const MeshWorld *mw, int mat) { return mat >= 0 && mat < mw->nmat ? &mw->mat[mat].rt : NULL; }

float *mw_solid_tris(const MeshWorld *mw, int *n) {
  float *out = (float *)malloc(sizeof(float) * 9 * ((size_t)mw->nsolid + 1)); int k = 0;
  if (!out) { *n = 0; return NULL; }
  for (int i = 0; i < mw->ntris; i++) if (mw->tris[i].flags & 2) memcpy(out + 9 * k++, mw->tris[i].p, 36);
  *n = k; return out;
}

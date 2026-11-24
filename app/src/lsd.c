/* lsd.c - see lsd.h. Layouts follow Figglewatts/libLSD (MIT): LBD.cs, TMD.cs, TMDPackets.cs, TIX.cs, TIM.cs. */
#include "lsd.h"
#include <stdlib.h>
#include <string.h>

uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

/* ------------------------------------------------------------------ TMD -> triangles */

typedef struct { LTri *t; int n, cap; } TriList;
static LTri *tl_add(TriList *l) {
  if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->t = (LTri *)realloc(l->t, (size_t)l->cap * sizeof(LTri)); }
  memset(&l->t[l->n], 0, sizeof(LTri)); return &l->t[l->n++];
}

/* texture page + (u,v) -> atlas texel, same maths as LibLSDUnity.MeshFromTMDObject */
static void uv_to_atlas(int page, int u, int v, float *ox, float *oy) {
  *ox = (float)((page % 16) * 128 - 640 + u);
  *oy = (float)(v + (page < 16 ? 0 : 256));
}

/* Parse the primitives of one TMD object into triangles. tmd = start of the TMD in the file buffer. */
static int build_object(const uint8_t *buf, size_t len, const uint8_t *objhdr, int fixp, uint32_t objTableTop, TriList *out) {
  uint32_t vaddr = rd32(objhdr + 0), nverts = rd32(objhdr + 4), paddr = rd32(objhdr + 16), nprims = rd32(objhdr + 20);
  uint32_t vpos = fixp ? vaddr : vaddr + objTableTop, ppos = fixp ? paddr : paddr + objTableTop;
  if ((size_t)vpos + (size_t)nverts * 8 > len || ppos > len) return -1;
  const uint8_t *vb = buf + vpos, *p = buf + ppos;
  int bad = 0;
  for (uint32_t i = 0; i < nprims; i++) {
    if ((size_t)(p - buf) + 4 > len) return -1;
    int ilen = p[1], flag = p[2], mode = p[3];
    const uint8_t *d = p + 4;
    int type = mode >> 5;
    if ((size_t)(d - buf) + (size_t)ilen * 4 > len) return -1;
    if (type == 1) {                                   /* polygon */
      int tex = (mode >> 2) & 1, quad = (mode >> 3) & 1, gour = (mode >> 4) & 1, ab = (mode >> 1) & 1;
      int unlit = flag & 1, grad = (flag >> 2) & 1;
      int nv = quad ? 4 : 3;
      int u[4] = {0}, vv[4] = {0}, page = 0, idx[4] = {0};
      uint8_t col[4][3]; int ncol = 0;
      const uint8_t *q = d;
      if (tex) {
        for (int k = 0; k < nv; k++) { u[k] = q[0]; vv[k] = q[1]; if (k == 1) page = rd16(q + 2) & 0x1f; q += 4; }
      }
      if (!tex || unlit) {
        ncol = unlit ? (gour ? nv : 1) : (grad ? nv : 1);
        for (int k = 0; k < ncol; k++) { col[k][0] = q[0]; col[k][1] = q[1]; col[k][2] = q[2]; q += 4; }
      }
      if (!unlit) {
        if (gour) { for (int k = 0; k < nv; k++) { idx[k] = rd16(q + 2); q += 4; } }
        else { q += 2; for (int k = 0; k < nv; k++) { idx[k] = rd16(q); q += 2; } }
      } else {
        for (int k = 0; k < nv; k++) { idx[k] = rd16(q); q += 2; }
      }
      if ((size_t)(q - d) > (size_t)ilen * 4 + 2) bad++;   /* layout mismatch (a trailing pad is allowed) */
      LVtx cv[4];
      int ok = 1;
      for (int k = 0; k < nv; k++) {
        if (idx[k] >= (int)nverts) { ok = 0; break; }
        const uint8_t *vp = vb + idx[k] * 8;
        cv[k].x = rds16(vp + 0) / 2048.0f; cv[k].y = -rds16(vp + 2) / 2048.0f; cv[k].z = rds16(vp + 4) / 2048.0f;
        if (ncol) { const uint8_t *c = col[ncol > 1 ? k : 0]; cv[k].r = c[0]; cv[k].g = c[1]; cv[k].b = c[2]; }
        else { cv[k].r = cv[k].g = cv[k].b = 128; }
        cv[k].a = 255;
        if (tex) uv_to_atlas(page, u[k], vv[k], &cv[k].u, &cv[k].v); else { cv[k].u = -1; cv[k].v = -1; }
      }
      if (ok) {
        static const int order[2][3] = {{0, 1, 2}, {2, 1, 3}};
        for (int t = 0; t < (quad ? 2 : 1); t++) {
          LTri *tr = tl_add(out);
          for (int k = 0; k < 3; k++) tr->v[k] = cv[order[t][k]];
          tr->alpha_blend = (uint8_t)ab;
        }
      }
    }
    p = d + ilen * 4;
  }
  return bad;
}

/* ------------------------------------------------------------------ LBD */

int lsd_load_chunk(const uint8_t *b, size_t len, LChunk *c) {
  memset(c, 0, sizeof *c);
  if (len < 0x20) return -1;
  int hasMML = rd16(b + 2) == 1; (void)hasMML;
  uint32_t addrOff = rd32(b + 4), tmdOff = rd32(b + 8);
  int nExtra = rd16(b + 26), tw = rd16(b + 28), th = rd16(b + 30);
  if (tw <= 0 || th <= 0 || tw > 256 || th > 256) return -2;
  uint32_t extraTop = 0x20 + (uint32_t)(tw * th) * 12;
  if ((size_t)extraTop + (size_t)nExtra * 12 > len) return -3;
  c->w = tw; c->h = th; c->nextra = nExtra;
  c->tiles = (LTile *)calloc((size_t)tw * th, sizeof(LTile));
  c->extra = (LTile *)calloc((size_t)nExtra + 1, sizeof(LTile));
  for (int i = 0; i < tw * th + nExtra; i++) {
    const uint8_t *t = b + 0x20 + (size_t)i * 12;
    LTile *o = i < tw * th ? &c->tiles[i] : &c->extra[i - tw * th];
    o->draw = t[0] == 1; o->solid = t[1] == 1; o->type = rd16(t + 2);
    o->footstep = t[4] & 0x7f; o->collision = t[4] >> 7; o->dir = t[5]; o->height = rds16(t + 6);
    uint32_t raw = rd32(t + 8);
    o->extra = raw == 0 ? -1 : (int)(((raw + addrOff) - extraTop) / 12);
  }
  /* tiles TMD */
  uint32_t ts = tmdOff + addrOff;
  if ((size_t)ts + 12 > len || rd32(b + ts) != 0x41) return -4;
  int fixp = rd32(b + ts + 4) & 1;
  uint32_t nobj = rd32(b + ts + 8);
  if (nobj > 4096) return -5;
  uint32_t objTop = ts + 12;
  if ((size_t)objTop + (size_t)nobj * 28 > len) return -6;
  c->nmesh = (int)nobj; c->mesh = (LMesh *)calloc(nobj ? nobj : 1, sizeof(LMesh));
  int totalBad = 0;
  for (uint32_t i = 0; i < nobj; i++) {
    TriList tl = {0};
    int r = build_object(b, len, b + objTop + i * 28, fixp, objTop, &tl);
    if (r < 0) { free(tl.t); lsd_free_chunk(c); return -7; }
    totalBad += r;
    c->mesh[i].tris = tl.t; c->mesh[i].ntris = tl.n;
  }
  if (totalBad) lsd_log("chunk: %d primitives had a layout mismatch", totalBad);
  return 0;
}

void lsd_free_chunk(LChunk *c) {
  for (int i = 0; i < c->nmesh; i++) { free(c->mesh[i].tris); free(c->mesh[i].upos); free(c->mesh[i].uidx); }
  free(c->mesh); free(c->tiles); free(c->extra); memset(c, 0, sizeof *c);
}

/* ------------------------------------------------------------------ TIX -> atlas */

static uint32_t col555(uint16_t c) {
  int r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31, stp = c >> 15;
  int black = !(r | g | bl);
  int alpha = 1;
  if (!stp && black) alpha = 0;                      /* black without the STP bit is transparent (cutout) */
  if (!alpha) return 0;
  return 0xFF000000u | ((uint32_t)(r * 255 / 31) << 16) | ((uint32_t)(g * 255 / 31) << 8) | (uint32_t)(bl * 255 / 31);
}

static void paint_tim(const uint8_t *tim, size_t avail, uint32_t *atlas) {
  if (avail < 8 || rd32(tim) != 0x10) return;
  uint32_t flags = rd32(tim + 4);
  int mode = flags & 7, hasclut = (flags >> 3) & 1;
  const uint8_t *p = tim + 8;
  const uint8_t *clut = NULL;
  if (hasclut) {
    uint32_t clen = rd32(p);
    clut = p + 12;                                   /* first CLUT */
    p += clen;
  }
  int px = rd16(p + 4), py = rd16(p + 6), w = rd16(p + 8), h = rd16(p + 10);
  const uint8_t *data = p + 12;
  int imgw = mode == 0 ? w * 4 : mode == 1 ? w * 2 : w;
  if (mode > 2) return;                              /* 24-bit not used by LSD levels */
  int ax = (px - 320) * 2;
  for (int r = 0; r < h; r++) {
    int ay = py + r;
    if (ay < 0 || ay >= VRAM_H) continue;
    for (int x = 0; x < imgw; x++) {
      int axx = ax + x;
      if (axx < 0 || axx >= VRAM_W) continue;
      uint32_t c;
      const uint8_t *row = data + (size_t)r * w * 2;
      if (mode == 1) { c = col555(rd16(clut + row[x] * 2)); }
      else if (mode == 0) { int byte = row[x >> 1]; int n = (x & 1) ? (byte >> 4) : (byte & 15); c = col555(rd16(clut + n * 2)); }
      else c = col555(rd16(row + x * 2));
      atlas[(size_t)ay * VRAM_W + axx] = c;
    }
  }
}

int lsd_load_atlas(const uint8_t *tix, size_t len, LAtlas *a) {
  a->px = (uint32_t *)malloc((size_t)VRAM_W * VRAM_H * 4);
  if (!a->px) return -1;
  for (size_t i = 0; i < (size_t)VRAM_W * VRAM_H; i++) a->px[i] = 0xFFFFFFFFu;
  if (len < 4) return -2;
  uint32_t nch = rd32(tix);
  if (nch > 64 || 4 + (size_t)nch * 8 > len) return -3;
  for (uint32_t c = 0; c < nch; c++) {
    uint32_t off = rd32(tix + 4 + c * 4);
    if ((size_t)off + 4 > len) continue;
    const uint8_t *ch = tix + off;
    uint32_t nt = rd32(ch);
    for (uint32_t t = 0; t < nt; t++) {
      uint32_t to = rd32(ch + 4 + t * 4);
      if ((size_t)off + to + 8 > len) continue;
      paint_tim(ch + to, len - off - to, a->px);
    }
  }
  return 0;
}

void lsd_free_atlas(LAtlas *a) { free(a->px); a->px = NULL; }

/* ------------------------------------------------------------------ water */

static float rdf(const uint8_t *p) { uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; float f; memcpy(&f, &v, 4); return f; }
static int rdb16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

int water_load(WaterSet *ws, const uint8_t *d, size_t len) {
  memset(ws, 0, sizeof *ws);
  if (len < 8 || memcmp(d, "WATR", 4)) return -1;
  int n = (int)(((uint32_t)d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]);
  if (n <= 0 || n > 16) return -2;
  ws->blob = (uint8_t *)malloc(len); memcpy(ws->blob, d, len);
  ws->t = (WaterTex *)calloc((size_t)n, sizeof(WaterTex));
  size_t o = 8;
  for (int i = 0; i < n; i++) {
    WaterTex *w = &ws->t[i];
    if (o + 30 > len) { water_free(ws); return -3; }
    for (int k = 0; k < 4; k++) w->rect[k] = rdf(ws->blob + o + k * 4);
    w->alpha = rdf(ws->blob + o + 16); w->speed = rdf(ws->blob + o + 20);
    w->mw = rdb16(ws->blob + o + 24); w->mh = rdb16(ws->blob + o + 26); w->pw = rdb16(ws->blob + o + 28);
    o += 30;
    size_t mapbytes = (size_t)w->mw * w->mh * 2, palbytes = (size_t)w->pw * 3;
    if (o + mapbytes + 4 * palbytes > len) { water_free(ws); return -4; }
    w->ra = ws->blob + o; o += mapbytes;
    for (int k = 0; k < 4; k++) { w->pal[k] = ws->blob + o; o += palbytes; }
    ws->n++;
  }
  return 0;
}

void water_free(WaterSet *ws) { free(ws->t); free(ws->blob); memset(ws, 0, sizeof *ws); }

/* mark triangles whose UVs fall in a water rectangle (UVMaterialOverride.UVRect, Unity UV space: x right, y up) */
void water_apply(LChunk *c, const WaterSet *ws) {
  for (int m = 0; m < c->nmesh; m++) for (int i = 0; i < c->mesh[m].ntris; i++) {
    LTri *tr = &c->mesh[m].tris[i];
    if (tr->v[0].u < 0) continue;
    for (int wi = 0; wi < ws->n; wi++) {
      const WaterTex *w = &ws->t[wi]; int inside = 0;
      float ux[3], uy[3];
      for (int k = 0; k < 3; k++) {
        ux[k] = (tr->v[k].u + 0.5f) / VRAM_W; uy[k] = (VRAM_H - tr->v[k].v - 0.5f) / VRAM_H;
        if (ux[k] >= w->rect[0] && ux[k] < w->rect[0] + w->rect[2] && uy[k] >= w->rect[1] && uy[k] < w->rect[1] + w->rect[3]) inside = 1;
      }
      if (!inside) continue;
      tr->water = (uint8_t)(wi + 1); tr->alpha_blend = 1;
      for (int k = 0; k < 3; k++) {
        float mx = (ux[k] - w->rect[0]) / w->rect[2], my = (uy[k] - w->rect[1]) / w->rect[3];
        mx = mx < 0 ? 0 : mx > 1 ? 1 : mx; my = my < 0 ? 0 : my > 1 ? 1 : my;
        tr->v[k].u = mx * (w->mw - 1); tr->v[k].v = (1.0f - my) * (w->mh - 1);
      }
      break;
    }
  }
}

/* The grey man (ETC/SYMSPY.MOM): one untextured TMD object. Revamped's prefab turns it upright with a 180 degree turn about
 * (0,1,-1), which maps (x, y, z) to (-x, -z, -y) in Unity coordinates, so do the same here. */
int lsd_load_mom_mesh(const uint8_t *b, size_t len, LMesh *out) {
  memset(out, 0, sizeof *out);
  if (len < 16 || memcmp(b, "MOM ", 4)) return -1;
  uint32_t ts = rd32(b + 8);
  if ((size_t)ts + 12 > len || rd32(b + ts) != 0x41) return -2;
  int fixp = rd32(b + ts + 4) & 1; uint32_t nobj = rd32(b + ts + 8), objTop = ts + 12;
  if (!nobj || (size_t)objTop + 28 > len) return -3;
  TriList tl = {0};
  if (build_object(b, len, b + objTop, fixp, objTop, &tl) < 0) { free(tl.t); return -4; }
  for (int i = 0; i < tl.n; i++) for (int k = 0; k < 3; k++) {
    LVtx *v = &tl.t[i].v[k]; float x = v->x, y = v->y, z = v->z;
    v->x = -x; v->y = -z; v->z = -y;
  }
  out->tris = tl.t; out->ntris = tl.n;
  return 0;
}

/* ------------------------------------------------------------------ standalone TIM -> ARGB (front end art) */
/* A whole TIM image (4/8/16-bit, first CLUT) as w*h ARGB8888, opaque (the front end art uses no cutout). NULL on error. */
uint32_t *lsd_decode_tim(const uint8_t *tim, size_t len, int *w_out, int *h_out) {
  if (len < 20 || rd32(tim) != 0x10) return NULL;
  uint32_t flags = rd32(tim + 4);
  int mode = flags & 7, hasclut = (flags >> 3) & 1;
  const uint8_t *p = tim + 8, *end = tim + len, *clut = NULL;
  if (hasclut) { uint32_t clen = rd32(p); clut = p + 12; p += clen; if (p + 12 > end) return NULL; }
  if (mode > 2 || (mode < 2 && !clut)) return NULL;
  int w = rd16(p + 8), h = rd16(p + 10);
  const uint8_t *data = p + 12;
  int imgw = mode == 0 ? w * 4 : mode == 1 ? w * 2 : w;
  if (imgw <= 0 || h <= 0 || data + (size_t)w * 2 * h > end) return NULL;
  uint32_t *px = (uint32_t *)malloc((size_t)imgw * h * 4);
  if (!px) return NULL;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = data + (size_t)r * w * 2;
    for (int x = 0; x < imgw; x++) {
      uint16_t c;
      if (mode == 2) c = rd16(row + x * 2);
      else if (mode == 1) c = rd16(clut + row[x] * 2);
      else { int byte = row[x >> 1]; c = rd16(clut + ((x & 1) ? (byte >> 4) : (byte & 15)) * 2); }
      int rr = c & 31, gg = (c >> 5) & 31, bb = (c >> 10) & 31;
      px[(size_t)r * imgw + x] = 0xFF000000u | ((uint32_t)(rr * 255 / 31) << 16) | ((uint32_t)(gg * 255 / 31) << 8) | (uint32_t)(bb * 255 / 31);
    }
  }
  *w_out = imgw; *h_out = h;
  return px;
}

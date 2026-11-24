/* model.c - entity visuals: models.bin loader, per-entity attachments (Ent.parts), stepped animation, rendering.
 *
 * models.bin (tools/models.py; tools/MODELS_FORMAT.md is the authoritative description). Big-endian, strings are
 * u8 length + bytes, no padding:
 *   "MODL" u32 version (1) u32 nmodels
 *   per model:
 *     str key                       "<lbd path relative to the dream folder>#<mom index>"
 *     u16 nmeshes, per mesh: u32 ntris, ntris x (3 x [f32 x,y,z, f32 u,v, u8 r,g,b,a], u8 alpha_blend)   (73 bytes)
 *     u16 nobjects (parents first), per object: u16 id, i16 parent index (-1 = model root), str name,
 *                   f32 pos[3], quat[4] (x,y,z,w), scale[3] (initial local pose), u16 nrefs, nrefs x u16 mesh index
 *     u16 nanims, per anim: u16 nframes, u16 length (frames; clip seconds = length / 20), u16 ntracks,
 *                   per track: u16 object index, u16 nkeys, nkeys x (u16 frame, f32 pos[3], quat[4], scale[3])
 * Vertices are in object space in Unity's frame (Y up, tile = 1 unit); u,v are texel positions in the dream's VRAM atlas
 * (u < 0: untextured, the vertex colour is drawn); colours use 128 = neutral like the level meshes. Keys are absolute local
 * TRS, stepped (hold the last key at or before the frame), clips loop (MOMHelper sets loopTime). */
#include "model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJ 1024

/* ---------------------------------------------------------------- runtime state */
typedef struct {
  const EntPart *src; const Model *m; const LMesh *mesh;   /* model instance (m) or loose mesh (mesh) */
  MTrs base;                      /* the part's TRS in the entity frame (LocalPosition can move it) */
  int vis;                        /* not below a deactivated GameObject */
  int clip, playing; float t;     /* clip -1: rest pose; t = seconds since Play; playing 0 = frozen (Stop) */
  MTrs *pose;                     /* per object initial pose with the prefab's overrides (NULL: the model's) */
  uint8_t *ovis;                  /* per object: not below a deactivated GameObject */
  float (*opos)[3]; uint8_t *has_opos;   /* LocalPosition overrides of objects (lazily allocated) */
} MPart;
typedef struct {
  int nparts; MPart *parts;
  int anim;                       /* part that this.PlayAnimation drives (the entity's AnimatedObject), -1 none */
  float p[3], q[4], s[3];         /* entity transform (Lua is the source of truth) */
  int renderers, was_active;
  char (*hidden)[96]; int nhidden;   /* deactivated GameObject paths below the entity */
  const char *child0;             /* name of the entity's first child (SetChildVisible) */
  float radius;                   /* bounding radius around the entity origin before entity scale */
} MEnt;

static Model *g_models; static int g_nmodels;
static const MeshWorld *g_mw;     /* mesh dream: materials of the model triangles (NULL: original dream, VRAM atlas) */
static MEnt *g_ent; static int g_nent;
static float *g_world;            /* scratch: one 3x4 matrix per object of the largest model */
static int g_maxobj;
static int st_ent, st_drawn, st_tris;

/* ---------------------------------------------------------------- big-endian reader with bounds checks */
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
static void quat_norm(float q[4]) {
  float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (l < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; } else for (int k = 0; k < 4; k++) q[k] /= l;
}
static void trs_from(MTrs *t, const float v[10]) { memcpy(t->p, v, 12); memcpy(t->q, v + 3, 16); memcpy(t->s, v + 7, 12); quat_norm(t->q); }
static void trs_(Rd *r, MTrs *t) { float v[10]; for (int k = 0; k < 10; k++) v[k] = f32_(r); trs_from(t, v); }

/* ---------------------------------------------------------------- matrices (row-major 3x4, column vectors) */
static void trs_matrix(float M[12], const float p[3], const float q[4], const float s[3]) {
  float x = q[0], y = q[1], z = q[2], w = q[3];
  float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
                 2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                 2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y) };
  for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) M[r * 4 + c] = R[r * 3 + c] * s[c]; M[r * 4 + 3] = p[r]; }
}
static void mat_mul(float out[12], const float A[12], const float B[12]) {   /* out = A * B (affine) */
  float t[12];
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) t[r * 4 + c] = A[r * 4] * B[c] + A[r * 4 + 1] * B[4 + c] + A[r * 4 + 2] * B[8 + c];
    t[r * 4 + 3] = A[r * 4] * B[3] + A[r * 4 + 1] * B[7] + A[r * 4 + 2] * B[11] + A[r * 4 + 3];
  }
  memcpy(out, t, sizeof t);
}
static float mat_scale(const float W[12]) {    /* largest column length: how much the matrix can stretch a radius */
  float a = W[0] * W[0] + W[4] * W[4] + W[8] * W[8], b = W[1] * W[1] + W[5] * W[5] + W[9] * W[9], c = W[2] * W[2] + W[6] * W[6] + W[10] * W[10];
  return sqrtf(fmaxf(a, fmaxf(b, c)));
}
static const float IDENT[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

/* ---------------------------------------------------------------- loading */
static void free_model(Model *m) {
  for (int k = 0; k < m->nmesh; k++) free(m->mesh[k].tris);
  for (int o = 0; o < m->nobj; o++) free(m->obj[o].mesh);
  for (int c = 0; c < m->nclip; c++) { for (int k = 0; k < m->clip[c].ntracks; k++) free(m->clip[c].tracks[k].keys); free(m->clip[c].tracks); free(m->clip[c].track_of); }
  free(m->mesh); free(m->obj); free(m->clip);
}

void models_free(void) {
  for (int i = 0; i < g_nmodels; i++) free_model(&g_models[i]);
  for (int i = 0; i < g_nent; i++) {
    MEnt *e = &g_ent[i];
    for (int k = 0; k < e->nparts; k++) { MPart *p = &e->parts[k]; free(p->pose); free(p->ovis); free(p->opos); free(p->has_opos); }
    free(e->parts); free(e->hidden);
  }
  free(g_models); free(g_ent); free(g_world);
  g_models = NULL; g_ent = NULL; g_world = NULL; g_nmodels = g_nent = g_maxobj = 0; g_mw = NULL;
}

static void *zalloc(size_t n, size_t sz) { return calloc(n ? n : 1, sz); }

static int read_model(Rd *r, Model *m) {
  str_(r, m->key, sizeof m->key);
  m->nmesh = (int)u16_(r);
  m->mesh = (LMesh *)zalloc((size_t)m->nmesh, sizeof(LMesh));
  for (int k = 0; k < m->nmesh; k++) {
    uint32_t nt = u32_(r);
    if (!need(r, (size_t)nt * 73)) return -1;
    LMesh *me = &m->mesh[k]; me->tris = (LTri *)zalloc(nt, sizeof(LTri)); me->ntris = (int)nt; m->ntris += (int)nt;
    for (uint32_t t = 0; t < nt; t++) {
      LTri *tr = &me->tris[t];
      for (int v = 0; v < 3; v++) {
        LVtx *vx = &tr->v[v];
        vx->x = f32_(r); vx->y = f32_(r); vx->z = f32_(r); vx->u = f32_(r); vx->v = f32_(r);
        vx->r = (uint8_t)u8_(r); vx->g = (uint8_t)u8_(r); vx->b = (uint8_t)u8_(r); vx->a = (uint8_t)u8_(r);
      }
      tr->alpha_blend = (uint8_t)u8_(r); tr->water = 0;
      if (tr->v[0].u >= 0) for (int v = 0; v < 3; v++) {      /* keep texels inside the atlas whatever the data says */
        LVtx *vx = &tr->v[v];
        vx->u = vx->u < 0 ? 0 : vx->u > VRAM_W - 1 ? VRAM_W - 1 : vx->u;
        vx->v = vx->v < 0 ? 0 : vx->v > VRAM_H - 1 ? VRAM_H - 1 : vx->v;
      }
    }
  }
  m->nobj = (int)u16_(r);
  if (m->nobj > MAX_OBJ) { m->nobj = 0; return -2; }
  m->obj = (MObj *)zalloc((size_t)m->nobj, sizeof(MObj));
  for (int o = 0; o < m->nobj && !r->bad; o++) {
    MObj *ob = &m->obj[o];
    ob->id = (int)u16_(r); ob->parent = (int16_t)u16_(r);
    if (ob->parent >= o) ob->parent = -1;                       /* the writer puts parents first; anything else is detached */
    str_(r, ob->name, sizeof ob->name);
    trs_(r, &ob->pose);
    ob->nmesh = (int)u16_(r); ob->mesh = (int *)zalloc((size_t)ob->nmesh, sizeof(int));
    int nm = 0;
    for (int k = 0; k < ob->nmesh; k++) { int mi = (int)u16_(r); if (mi < m->nmesh) ob->mesh[nm++] = mi; }
    ob->nmesh = nm;
    if (ob->parent >= 0) snprintf(ob->path, sizeof ob->path, "%.63s/%.31s", m->obj[ob->parent].path, ob->name);
    else snprintf(ob->path, sizeof ob->path, "%s", ob->name);
  }
  m->nclip = (int)u16_(r);
  m->clip = (MClip *)zalloc((size_t)m->nclip, sizeof(MClip));
  for (int c = 0; c < m->nclip && !r->bad; c++) {
    MClip *cl = &m->clip[c];
    cl->nframes = (int)u16_(r); cl->length = (int)u16_(r); cl->ntracks = (int)u16_(r); cl->fps = MODEL_FPS; cl->loop = 1;
    cl->tracks = (MTrack *)zalloc((size_t)cl->ntracks, sizeof(MTrack));
    cl->track_of = (int *)zalloc((size_t)m->nobj, sizeof(int));
    for (int o = 0; o < m->nobj; o++) cl->track_of[o] = -1;
    for (int k = 0; k < cl->ntracks && !r->bad; k++) {
      MTrack *tk = &cl->tracks[k];
      tk->obj = (int)u16_(r); tk->nkeys = (int)u16_(r);
      if (!need(r, (size_t)tk->nkeys * 42)) return -3;        /* u16 + 10 f32 per key */
      tk->keys = (MKey *)zalloc((size_t)tk->nkeys, sizeof(MKey));
      for (int j = 0; j < tk->nkeys; j++) { tk->keys[j].frame = (int)u16_(r); trs_(r, &tk->keys[j].t); }
      for (int j = 1; j < tk->nkeys; j++) if (tk->keys[j].frame < tk->keys[j - 1].frame) {   /* keep sorted */
        MKey kk = tk->keys[j]; int a = j - 1; while (a >= 0 && tk->keys[a].frame > kk.frame) { tk->keys[a + 1] = tk->keys[a]; a--; } tk->keys[a + 1] = kk; }
      if (tk->obj < m->nobj && tk->nkeys) cl->track_of[tk->obj] = k; else tk->obj = -1;
    }
  }
  return r->bad ? -4 : 0;
}

/* rest-pose bounding radius of a model around its root */
static void model_bounds(Model *m) {
  m->radius = 0.1f;
  for (int o = 0; o < m->nobj; o++) {
    const MObj *ob = &m->obj[o]; float L[12];
    trs_matrix(L, ob->pose.p, ob->pose.q, ob->pose.s);
    float *W = g_world + o * 12;
    mat_mul(W, ob->parent >= 0 ? g_world + ob->parent * 12 : IDENT, L);
    for (int k = 0; k < ob->nmesh; k++) {
      const LMesh *me = &m->mesh[ob->mesh[k]];
      for (int t = 0; t < me->ntris; t++) for (int v = 0; v < 3; v++) {
        const LVtx *vx = &me->tris[t].v[v];
        float x = W[0] * vx->x + W[1] * vx->y + W[2] * vx->z + W[3], y = W[4] * vx->x + W[5] * vx->y + W[6] * vx->z + W[7], z = W[8] * vx->x + W[9] * vx->y + W[10] * vx->z + W[11];
        float d = sqrtf(x * x + y * y + z * z); if (d > m->radius) m->radius = d;
      }
    }
  }
}

static float mesh_radius(const LMesh *me) {
  float r = 0.1f;
  for (int t = 0; t < me->ntris; t++) for (int v = 0; v < 3; v++) { const LVtx *x = &me->tris[t].v[v]; float d = sqrtf(x->x * x->x + x->y * x->y + x->z * x->z); if (d > r) r = d; }
  return r;
}

/* Every MOM ends with a box of triangles whose texels are all fully transparent (cutout): drop textured triangles whose
 * samples (corners, edge midpoints, centre) all hit alpha 0 in this dream's atlas, so they cost nothing per frame */
static int drop_invisible(Model *m, const LAtlas *atlas) {
  if (!atlas || !atlas->px) return 0;
  static const float bw[10][3] = { {1,0,0}, {0,1,0}, {0,0,1}, {.5f,.5f,0}, {0,.5f,.5f}, {.5f,0,.5f}, {.34f,.33f,.33f}, {.6f,.2f,.2f}, {.2f,.6f,.2f}, {.2f,.2f,.6f} };
  int dropped = 0;
  for (int k = 0; k < m->nmesh; k++) {
    LMesh *me = &m->mesh[k]; int n = 0;
    for (int t = 0; t < me->ntris; t++) {
      const LTri *tr = &me->tris[t]; int visible = tr->v[0].u < 0;
      for (int s = 0; s < 10 && !visible; s++) {
        int u = (int)(bw[s][0] * tr->v[0].u + bw[s][1] * tr->v[1].u + bw[s][2] * tr->v[2].u), v = (int)(bw[s][0] * tr->v[0].v + bw[s][1] * tr->v[1].v + bw[s][2] * tr->v[2].v);
        if (u >= 0 && u < VRAM_W && v >= 0 && v < VRAM_H && (atlas->px[(size_t)v * VRAM_W + u] >> 24)) visible = 1;
      }
      if (visible) me->tris[n++] = *tr; else dropped++;
    }
    me->ntris = n;
  }
  m->ntris -= dropped;
  return dropped;
}

static const Model *find_model(const char *key) { for (int k = 0; k < g_nmodels; k++) if (!strcmp(g_models[k].key, key)) return &g_models[k]; return NULL; }

/* is `path` at or below the deactivated GameObject path `h`? */
static int under(const char *path, const char *h) { size_t n = strlen(h); return !strncmp(path, h, n) && (path[n] == 0 || path[n] == '/'); }
static void full_path(char *out, size_t cap, const MPart *p, const char *objpath) {
  if (!strcmp(objpath, ".")) snprintf(out, cap, "%s", p->src->path);          /* amodels: object 0 is the part itself */
  else if (!strcmp(p->src->path, ".")) snprintf(out, cap, "%s", objpath); else snprintf(out, cap, "%s/%s", p->src->path, objpath);
}
static int hidden(const MEnt *e, const char *path) { for (int k = 0; k < e->nhidden; k++) if (under(path, e->hidden[k])) return 1; return 0; }

static void refresh_visibility(MEnt *e) {
  for (int k = 0; k < e->nparts; k++) {
    MPart *p = &e->parts[k];
    p->vis = strcmp(p->src->path, ".") ? !hidden(e, p->src->path) : 1;
    if (p->m) for (int o = 0; o < p->m->nobj; o++) { char fp[200]; full_path(fp, sizeof fp, p, p->m->obj[o].path); p->ovis[o] = !hidden(e, fp); }
  }
}
static void set_hidden(MEnt *e, const char *path, int hide) {
  for (int k = 0; k < e->nhidden; k++) if (!strcmp(e->hidden[k], path)) {
    if (!hide) { e->hidden[k][0] = 0; memmove(e->hidden[k], e->hidden[k + 1], sizeof e->hidden[0] * (size_t)(e->nhidden - k - 1)); e->nhidden--; refresh_visibility(e); }
    return;
  }
  if (!hide) return;
  e->hidden = (char (*)[96])realloc(e->hidden, sizeof e->hidden[0] * (size_t)(e->nhidden + 1));
  snprintf(e->hidden[e->nhidden++], 96, "%s", path);
  refresh_visibility(e);
}

static void attach_entities(const Dream *d, int *pbound, int *pmissing);
int models_load(const Dream *d, const LAtlas *atlas) {
  models_free();
  if (!d) return 0;
  char path[64]; snprintf(path, sizeof path, "%s/models.bin", d->folder);
  size_t n = 0; uint8_t *buf = lsd_read_file(path, &n);
  int rc = 0, ntris = 0;
  if (buf) {
    Rd r = { buf, buf + n, 0 };
    if (n < 12 || memcmp(buf, "MODL", 4)) rc = -1;
    else {
      r.p += 4; uint32_t ver = u32_(&r), nm = u32_(&r);
      if (ver != 1) lsd_log("models.bin: version %u, expected 1", (unsigned)ver);
      if (nm > 4096) nm = 0;
      g_models = (Model *)zalloc(nm, sizeof(Model));
      for (uint32_t i = 0; i < nm && !r.bad; i++) { rc = read_model(&r, &g_models[g_nmodels++]); if (rc) break; }
      for (int i = 0; i < g_nmodels; i++) { Model *m = &g_models[i]; if (m->nobj > g_maxobj) g_maxobj = m->nobj;
        for (int c = 0; c < m->nclip; c++) if (!m->clip[c].track_of) { m->clip[c].track_of = (int *)zalloc((size_t)m->nobj, sizeof(int)); for (int o = 0; o < m->nobj; o++) m->clip[c].track_of[o] = -1; } }
    }
    free(buf);
    if (rc) lsd_log("models.bin: corrupt (rc=%d) in model %d", rc, g_nmodels - 1);
  }
  g_world = (float *)malloc(sizeof(float) * 12 * (size_t)(g_maxobj > 0 ? g_maxobj : 1));
  int ndrop = 0;
  for (int i = 0; i < g_nmodels; i++) { ndrop += drop_invisible(&g_models[i], atlas); model_bounds(&g_models[i]); ntris += g_models[i].ntris; }
  int nbound, nmissing; attach_entities(d, &nbound, &nmissing);
  if (buf || g_nmodels) lsd_log("models: %d in %s (%d tris, %d invisible dropped, rc=%d), %d entities with visuals, %d parts unresolved", g_nmodels, path, ntris, ndrop, rc, nbound, nmissing);
  return rc;
}

/* amodels.bin model (ADDL_FORMAT.md 4): like models.bin with the 76-byte world.bin triangle (u16 mat, u8 flags, u8 0,
 * 3 x [f32 x y z u v, u8 r g b a]), objects without ids (object 0 = the model root, its own GameObject: path "."), named clips
 * with their fps and loop flag */
static int read_amodel(Rd *r, Model *m) {
  str_(r, m->key, sizeof m->key);
  m->nmesh = (int)u16_(r);
  m->mesh = (LMesh *)zalloc((size_t)m->nmesh, sizeof(LMesh));
  for (int k = 0; k < m->nmesh; k++) {
    uint32_t nt = u32_(r);
    if (!need(r, (size_t)nt * 76)) return -1;
    LMesh *me = &m->mesh[k]; me->tris = (LTri *)zalloc(nt, sizeof(LTri)); me->ntris = (int)nt; m->ntris += (int)nt;
    for (uint32_t t = 0; t < nt; t++) {
      LTri *tr = &me->tris[t];
      tr->mat = (uint16_t)u16_(r); u8_(r); u8_(r); tr->alpha_blend = 0; tr->water = 0;
      if (g_mw && tr->mat >= g_mw->nmat) tr->mat = 0;
      for (int v = 0; v < 3; v++) {
        LVtx *vx = &tr->v[v];
        vx->x = f32_(r); vx->y = f32_(r); vx->z = f32_(r); vx->u = f32_(r); vx->v = f32_(r);
        vx->r = (uint8_t)u8_(r); vx->g = (uint8_t)u8_(r); vx->b = (uint8_t)u8_(r); vx->a = (uint8_t)u8_(r);
      }
      if (g_mw && g_mw->mat[tr->mat].shader == 1) tr->alpha_blend = 1;
    }
  }
  m->nobj = (int)u16_(r);
  if (m->nobj > MAX_OBJ) { m->nobj = 0; return -2; }
  m->obj = (MObj *)zalloc((size_t)m->nobj, sizeof(MObj));
  for (int o = 0; o < m->nobj && !r->bad; o++) {
    MObj *ob = &m->obj[o];
    ob->id = o; ob->parent = (int16_t)u16_(r);
    if (ob->parent >= o) ob->parent = -1;
    str_(r, ob->name, sizeof ob->name);
    trs_(r, &ob->pose); u8_(r);
    ob->nmesh = (int)u16_(r); ob->mesh = (int *)zalloc((size_t)ob->nmesh, sizeof(int));
    int nm = 0;
    for (int k = 0; k < ob->nmesh; k++) { int mi = (int)u16_(r); if (mi < m->nmesh) ob->mesh[nm++] = mi; }
    ob->nmesh = nm;
    if (o == 0 || ob->parent < 0) snprintf(ob->path, sizeof ob->path, ".");
    else if (ob->parent == 0) snprintf(ob->path, sizeof ob->path, "%s", ob->name);
    else snprintf(ob->path, sizeof ob->path, "%.63s/%.31s", m->obj[ob->parent].path, ob->name);
  }
  m->nclip = (int)u16_(r);
  m->clip = (MClip *)zalloc((size_t)m->nclip, sizeof(MClip));
  for (int c = 0; c < m->nclip && !r->bad; c++) {
    MClip *cl = &m->clip[c];
    str_(r, cl->name, sizeof cl->name);
    cl->fps = f32_(r); if (cl->fps <= 0) cl->fps = 24.0f;
    cl->length = (int)u16_(r); cl->nframes = cl->length + 1; cl->loop = (int)u8_(r); cl->ntracks = (int)u16_(r);
    cl->tracks = (MTrack *)zalloc((size_t)cl->ntracks, sizeof(MTrack));
    cl->track_of = (int *)zalloc((size_t)m->nobj, sizeof(int));
    for (int o = 0; o < m->nobj; o++) cl->track_of[o] = -1;
    for (int k = 0; k < cl->ntracks && !r->bad; k++) {
      MTrack *tk = &cl->tracks[k];
      tk->obj = (int)u16_(r); tk->nkeys = (int)u16_(r);
      if (!need(r, (size_t)tk->nkeys * 42)) return -3;
      tk->keys = (MKey *)zalloc((size_t)tk->nkeys, sizeof(MKey));
      for (int j = 0; j < tk->nkeys; j++) { tk->keys[j].frame = (int)u16_(r); trs_(r, &tk->keys[j].t); }
      if (tk->obj < m->nobj && tk->nkeys) cl->track_of[tk->obj] = k; else tk->obj = -1;
    }
  }
  return r->bad ? -4 : 0;
}

int models_load_mesh(const Dream *d, const MeshWorld *mw) {
  models_free();
  if (!d) return 0;
  g_mw = mw;
  size_t n = 0; uint8_t *buf = d->models[0] ? lsd_read_file(d->models, &n) : NULL;
  int rc = 0, ntris = 0;
  if (buf) {
    Rd r = { buf, buf + n, 0 };
    if (n < 12 || memcmp(buf, "AMDL", 4)) rc = -1;
    else {
      r.p += 4; uint32_t ver = u32_(&r), nm = u32_(&r);
      if (ver != 1) lsd_log("amodels.bin: version %u, expected 1", (unsigned)ver);
      if (nm > 4096) nm = 0;
      g_models = (Model *)zalloc(nm, sizeof(Model));
      for (uint32_t i = 0; i < nm && !r.bad; i++) { rc = read_amodel(&r, &g_models[g_nmodels++]); if (rc) break; }
      for (int i = 0; i < g_nmodels; i++) if (g_models[i].nobj > g_maxobj) g_maxobj = g_models[i].nobj;
    }
    free(buf);
    if (rc) lsd_log("amodels.bin: corrupt (rc=%d) in model %d", rc, g_nmodels - 1);
  }
  g_world = (float *)malloc(sizeof(float) * 12 * (size_t)(g_maxobj > 0 ? g_maxobj : 1));
  for (int i = 0; i < g_nmodels; i++) { model_bounds(&g_models[i]); ntris += g_models[i].ntris; }
  int nbound, nmissing; attach_entities(d, &nbound, &nmissing);
  lsd_log("models: %d in %s (%d tris, rc=%d), %d entities with visuals, %d parts unresolved", g_nmodels, d->models, ntris, rc, nbound, nmissing);
  return rc;
}

static void attach_entities(const Dream *d, int *pbound, int *pmissing) {
  g_nent = d->nent; g_ent = (MEnt *)zalloc((size_t)g_nent, sizeof(MEnt));
  int nbound = 0, nmissing = 0;
  for (int i = 0; i < d->nent; i++) {
    const Ent *en = &d->ent[i]; MEnt *e = &g_ent[i];
    e->anim = -1; e->renderers = 1; e->was_active = en->active0; e->child0 = en->child0;
    float yaw = en->yaw0 * 0.0174532925f;                  /* the prefab transform (scripts may have moved it on an earlier visit) */
    memcpy(e->p, en->pos0, 12); e->q[0] = 0; e->q[1] = sinf(yaw * 0.5f); e->q[2] = 0; e->q[3] = cosf(yaw * 0.5f);
    for (int k = 0; k < 3; k++) e->s[k] = en->scale0[k] != 0 ? en->scale0[k] : 1;
    if (!en->nparts) continue;
    e->parts = (MPart *)zalloc((size_t)en->nparts, sizeof(MPart));
    for (int k = 0; k < en->nparts; k++) {
      const EntPart *sp = &en->parts[k]; const Model *m = find_model(sp->key);
      if (!m || (sp->is_mesh && (sp->tmd < 0 || sp->tmd >= m->nmesh))) { nmissing++; continue; }
      MPart *p = &e->parts[e->nparts++];
      p->src = sp; trs_from(&p->base, sp->t); p->clip = -1; p->vis = 1;
      float B[12]; trs_matrix(B, p->base.p, p->base.q, p->base.s);
      float reach = sqrtf(p->base.p[0] * p->base.p[0] + p->base.p[1] * p->base.p[1] + p->base.p[2] * p->base.p[2]);
      if (sp->is_mesh) { p->mesh = &m->mesh[sp->tmd]; reach += mesh_radius(p->mesh) * mat_scale(B); }
      else {
        p->m = m; reach += m->radius * mat_scale(B);
        p->ovis = (uint8_t *)malloc((size_t)m->nobj + 1); memset(p->ovis, 1, (size_t)m->nobj + 1);
        if (sp->npose) {                                    /* the prefab copy's own poses for some objects */
          p->pose = (MTrs *)malloc(sizeof(MTrs) * (size_t)(m->nobj ? m->nobj : 1));
          for (int o = 0; o < m->nobj; o++) p->pose[o] = m->obj[o].pose;
          for (int j = 0; j < sp->npose; j++) for (int o = 0; o < m->nobj; o++) if (!strcmp(m->obj[o].name, sp->pose[j].name)) { trs_from(&p->pose[o], sp->pose[j].t); break; }
        }
        if (e->anim < 0 || !strcmp(sp->path, ".")) e->anim = e->nparts - 1;   /* the entity's own AnimatedObject wins */
        if (sp->autoplay >= 0 && sp->autoplay < m->nclip) { p->clip = sp->autoplay; p->playing = 1; }
      }
      if (reach > e->radius) e->radius = reach;
    }
    /* initially inactive GameObjects of the prefab */
    for (int k = 0; k < e->nparts; k++) {
      MPart *p = &e->parts[k];
      if (!p->src->active && strcmp(p->src->path, ".")) set_hidden(e, p->src->path, 1);
      if (p->m) for (int j = 0; j < p->src->npose; j++) if (!p->src->pose[j].active)
        for (int o = 0; o < p->m->nobj; o++) if (!strcmp(p->m->obj[o].name, p->src->pose[j].name)) { char fp[200]; full_path(fp, sizeof fp, p, p->m->obj[o].path); set_hidden(e, fp, 1); break; }
    }
    if (en->aobj[0]) for (int k = 0; k < e->nparts; k++) if (e->parts[k].m && !strcmp(e->parts[k].src->path, en->aobj)) { e->anim = k; break; }   /* aobj line */
    refresh_visibility(e);
    if (e->nparts) nbound++;
  }
  *pbound = nbound; *pmissing = nmissing;
}

/* ---------------------------------------------------------------- animation */
static const MKey *key_at(const MTrack *tk, int frame) {   /* last key with key.frame <= frame (the first key before it) */
  int lo = 0, hi = tk->nkeys - 1, best = 0;
  while (lo <= hi) { int mid = (lo + hi) >> 1; if (tk->keys[mid].frame <= frame) { best = mid; lo = mid + 1; } else hi = mid - 1; }
  return &tk->keys[best];
}
static int clip_period(const MClip *c) { return c->length > 0 ? c->length : 1; }

void models_update(float dt, const Dream *d) {
  if (!g_ent || !d) return;
  int n = g_nent < d->nent ? g_nent : d->nent;
  for (int i = 0; i < n; i++) {
    MEnt *e = &g_ent[i]; if (!e->nparts) continue;
    int act = d->ent[i].live;
    for (int k = 0; k < e->nparts; k++) {
      MPart *p = &e->parts[k]; if (!p->m) continue;
      if (act && !e->was_active && p->src->autoplay >= 0 && p->src->autoplay < p->m->nclip) { p->clip = p->src->autoplay; p->t = 0; p->playing = 1; }   /* AnimatedObject.OnEnable */
      if (p->playing && act && p->clip >= 0) {             /* a float add per playing model; poses are evaluated when drawn */
        p->t += dt;
        const MClip *cl = &p->m->clip[p->clip];
        float len = clip_period(cl) / cl->fps;
        if (!cl->loop) { if (p->t > len) p->t = len; }       /* play once, hold the last frame */
        else if (p->t >= len * 64) p->t = fmodf(p->t, len);   /* keep float precision on long runs */
      }
    }
    e->was_active = act;
  }
}

/* ---------------------------------------------------------------- rendering */
static void draw_part(Frame *f, const Cam *cam, const MPart *p, const float E[12], const uint32_t *atlas, RenderStats *st) {
  float P[12], B[12];
  trs_matrix(B, p->base.p, p->base.q, p->base.s); mat_mul(P, E, B);
  if (p->mesh) { if (g_mw) render_mesh_mw(f, cam, p->mesh, P, g_mw, st); else render_mesh_xf(f, cam, p->mesh, P, atlas, st); st_tris += p->mesh->ntris; return; }
  const Model *m = p->m; const MClip *cl = NULL; int frame = 0;
  if (p->clip >= 0 && p->clip < m->nclip) {
    cl = &m->clip[p->clip]; frame = (int)(p->t * cl->fps);
    if (cl->loop) frame %= clip_period(cl); else if (frame > cl->length) frame = cl->length;
  }
  for (int o = 0; o < m->nobj; o++) {
    const MObj *ob = &m->obj[o];
    if (!p->ovis[o]) continue;                               /* a deactivated object hides its children too (paths) */
    MTrs t = p->pose ? p->pose[o] : ob->pose;
    if (p->has_opos && p->has_opos[o]) memcpy(t.p, p->opos[o], 12);
    if (cl) { int k = cl->track_of[o]; if (k >= 0) t = key_at(&cl->tracks[k], frame)->t; }   /* the Animator overrides scripts */
    float L[12]; trs_matrix(L, t.p, t.q, t.s);
    float *W = g_world + o * 12;
    mat_mul(W, ob->parent >= 0 ? g_world + ob->parent * 12 : P, L);
    for (int k = 0; k < ob->nmesh; k++) { const LMesh *me = &m->mesh[ob->mesh[k]];
      if (g_mw) render_mesh_mw(f, cam, me, W, g_mw, st); else render_mesh_xf(f, cam, me, W, atlas, st); st_tris += me->ntris; }
  }
}

void models_render(Frame *f, const Cam *cam, const Dream *d, const LAtlas *atlas, RenderStats *st) {
  st_ent = st_drawn = st_tris = 0;
  if (!g_ent || !d) return;
  float R[3], U[3], F[3]; cam_basis(cam, R, U, F);
  float halfx = (f->w * 0.5f) / cam->focal, halfy = (f->h * 0.5f) / cam->focal;
  int n = g_nent < d->nent ? g_nent : d->nent;
  for (int i = 0; i < n; i++) {
    MEnt *e = &g_ent[i]; if (!e->nparts) continue;
    st_ent++;
    if (!d->ent[i].live) continue;
    float sc = fmaxf(fmaxf(fabsf(e->s[0]), fabsf(e->s[1])), fabsf(e->s[2]));
    float rad = e->radius * sc * 1.5f + 0.25f;               /* slack for animation */
    float dx = e->p[0] - cam->x, dy = e->p[1] - cam->y, dz = e->p[2] - cam->z;
    if (dx * dx + dz * dz > (cam->draw_dist + rad) * (cam->draw_dist + rad)) continue;
    float ccz = dx * F[0] + dy * F[1] + dz * F[2];
    if (ccz < -rad) continue;                                /* behind the camera */
    float ccx = dx * R[0] + dy * R[1] + dz * R[2], ccy = dx * U[0] + dy * U[1] + dz * U[2], zz = ccz > 0 ? ccz : 0;
    if (fabsf(ccx) > zz * halfx + rad * 1.5f || fabsf(ccy) > zz * halfy + rad * 1.5f) continue;   /* outside the view cone */
    float E[12]; trs_matrix(E, e->p, e->q, e->s);
    int drawn = 0;
    for (int k = 0; k < e->nparts; k++) {
      const MPart *p = &e->parts[k];
      if (!p->vis || (!e->renderers && (p->m || !strcmp(p->src->path, ".")))) continue;
      draw_part(f, cam, p, E, atlas ? atlas->px : NULL, st); drawn = 1;
    }
    st_drawn += drawn;
  }
}

void models_stats(int *nent, int *ndrawn, int *ntris) { *nent = st_ent; *ndrawn = st_drawn; *ntris = st_tris; }

/* ---------------------------------------------------------------- script hooks */
static MEnt *ent_of(int i) { return g_ent && i >= 0 && i < g_nent && g_ent[i].nparts ? &g_ent[i] : NULL; }

/* the part a path names: exactly a part, or an object inside a model part (obj set), NULL if neither */
static MPart *resolve(MEnt *e, const char *path, int *obj) {
  *obj = -1;
  for (int k = 0; k < e->nparts; k++) if (!strcmp(e->parts[k].src->path, path)) return &e->parts[k];
  for (int k = 0; k < e->nparts; k++) {
    MPart *p = &e->parts[k]; if (!p->m) continue;
    for (int o = 0; o < p->m->nobj; o++) { char fp[200]; full_path(fp, sizeof fp, p, p->m->obj[o].path); if (!strcmp(fp, path)) { *obj = o; return p; } }
  }
  return NULL;
}
static MPart *anim_part(MEnt *e, const char *path) {
  if (!path) return e->anim >= 0 ? &e->parts[e->anim] : NULL;
  int o; MPart *p = resolve(e, path, &o);
  return p && p->m ? p : NULL;
}

int model_has(int ent) { return ent_of(ent) != NULL; }
void model_set_xform(int ent, const float p[3], const float q[4], const float s[3]) {
  MEnt *e = g_ent && ent >= 0 && ent < g_nent ? &g_ent[ent] : NULL; if (!e) return;
  memcpy(e->p, p, 12); memcpy(e->q, q, 16); memcpy(e->s, s, 12); quat_norm(e->q);
}
/* AnimatedObject.Clips index -> animation of the model (mclips line of a mesh dream; identity otherwise) */
static int map_clip(const MPart *p, int clip) {
  if (!p->src->nclipmap) return clip;
  return clip >= 0 && clip < p->src->nclipmap ? p->src->clipmap[clip] : -1;
}
void model_play(int ent, const char *path, int clip) {
  MEnt *e = ent_of(ent); MPart *p = e ? anim_part(e, path) : NULL; if (!p) return;
  int c = map_clip(p, clip);
  if (c < 0 || c >= p->m->nclip) { lsd_log("model %s: no clip %d (%d clips)", p->m->key, clip, p->m->nclip); return; }
  p->clip = c; p->t = 0; p->playing = 1;
}
void model_stop(int ent, const char *path) { MEnt *e = ent_of(ent); MPart *p = e ? anim_part(e, path) : NULL; if (p) p->playing = 0; }
void model_resume(int ent, const char *path) { MEnt *e = ent_of(ent); MPart *p = e ? anim_part(e, path) : NULL; if (p) p->playing = 1; }
float model_clip_seconds(int ent, const char *path, int clip) {
  MEnt *e = ent_of(ent); MPart *p = e ? anim_part(e, path) : NULL;
  if (!p) return -1;
  clip = map_clip(p, clip);
  if (clip < 0 || clip >= p->m->nclip) return -1;
  return p->m->clip[clip].length / p->m->clip[clip].fps;
}
int model_find(int ent, const char *path) {
  MEnt *e = ent_of(ent); if (!e || !path || !path[0]) return 0;
  for (int k = 0; k < e->nparts; k++) {
    MPart *p = &e->parts[k];
    if (under(p->src->path, path)) return 1;                 /* the part or a GameObject above it */
    if (p->m) for (int o = 0; o < p->m->nobj; o++) { char fp[200]; full_path(fp, sizeof fp, p, p->m->obj[o].path); if (under(fp, path)) return 1; }
  }
  return 0;
}
void model_set_active(int ent, const char *path, int active) {
  MEnt *e = ent_of(ent); if (!e) return;
  if (!path) path = e->child0;                               /* SetChildVisible: transform.GetChild(0) */
  if (!path || !path[0] || !strcmp(path, ".")) return;
  set_hidden(e, path, !active);
}
/* SetRenderersActive: every mesh-drawing GameObject below the entity, inactive ones included. The meshes of model objects
 * are child GameObjects that are otherwise always active, so a flag gates them; loose meshes are GameObjects of their own */
void model_set_renderers(int ent, int on) {
  MEnt *e = ent_of(ent); if (!e) return;
  if (!g_mw) {                                               /* original dreams: as before */
    e->renderers = on != 0;
    for (int k = 0; k < e->nparts; k++) if (e->parts[k].mesh && strcmp(e->parts[k].src->path, ".")) set_hidden(e, e->parts[k].src->path, !on);
    return;
  }
  /* mesh dreams (InteractiveObjectProxy: every GameObject with a Renderer below the entity, inactive ones too, is
   * SetActive(on)): hide/show exactly the mesh-drawing objects, so a later SetActive(true) of one of them (popup.lua shows
   * a single-node popup model this way) brings it back */
  for (int k = 0; k < e->nparts; k++) {
    MPart *p = &e->parts[k];
    if (p->mesh) { if (strcmp(p->src->path, ".")) set_hidden(e, p->src->path, !on); continue; }
    for (int o = 0; o < p->m->nobj; o++) if (p->m->obj[o].nmesh) { char fp[200]; full_path(fp, sizeof fp, p, p->m->obj[o].path); set_hidden(e, fp, !on); }
  }
}
void model_set_local_pos(int ent, const char *path, const float pos[3]) {
  MEnt *e = ent_of(ent); if (!e || !path) return;
  int o; MPart *p = resolve(e, path, &o); if (!p) return;
  if (o < 0) { memcpy(p->base.p, pos, 12); return; }        /* a part: move its root */
  if (!p->opos) { p->opos = (float (*)[3])zalloc((size_t)p->m->nobj, sizeof *p->opos); p->has_opos = (uint8_t *)zalloc((size_t)p->m->nobj, 1); }
  if (!p->opos || !p->has_opos) return;
  memcpy(p->opos[o], pos, 12); p->has_opos[o] = 1;
}


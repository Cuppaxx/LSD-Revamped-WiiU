/* sfx.c - see sfx.h */
#include "sfx.h"
#include "lsd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned rng_next(unsigned *s);

typedef struct { char path[48]; AClip c; int ok; } GClip;
static GClip **gc; static int ngc;                                   /* each clip allocated on its own: pointers stay valid */
typedef struct { const AClip *c; float pitch; int set; } Foot;      /* c NULL with set: silence */
static Foot lbd_foot[128], lbd_fallback, uv_fallback;
typedef struct { int x, y, w, h; Foot f; } UVRect;
static UVRect *uvr; static int nuvr; static int uv_w = 1024, uv_h = 1024;
static const AClip *link_clip, *fall_clip, *test_clip; static float link_p[8], fall_p[8]; static int nlink, nfall;
static int test_voice = -1;
static char modsong_folder[32][8]; static int modsong_idx[32], nmodsong;
int g_sfx_songs_per_dream = 5, g_sfx_day1_song = 2;
int g_foot_log;

const AClip *sfx_clip(const char *path) {
  if (!path || !path[0] || !strcmp(path, "-")) return NULL;
  for (int i = 0; i < ngc; i++) if (!strcmp(gc[i]->path, path)) return gc[i]->ok ? &gc[i]->c : NULL;
  GClip *g = (GClip *)calloc(1, sizeof(GClip)); if (!g) return NULL;
  snprintf(g->path, sizeof g->path, "%s", path);
  size_t n; uint8_t *b = lsd_read_file(path, &n);
  if (b) { g->ok = audio_clip_load(b, n, &g->c) == 0; free(b); }
  if (!g->ok) lsd_log("sfx: missing sound %s", path);
  GClip **ng = (GClip **)realloc(gc, sizeof(GClip *) * (size_t)(ngc + 1)); if (!ng) { audio_clip_free(&g->c); free(g); return NULL; }
  gc = ng; gc[ngc++] = g;
  return g->ok ? &g->c : NULL;
}

static Foot foot_of(const char *clip, float pitch) { Foot f; f.c = sfx_clip(clip); f.pitch = pitch > 0 ? pitch : 1; f.set = 1; return f; }

void sfx_init(void) {
  size_t len; uint8_t *buf = lsd_read_file("audio.txt", &len);
  if (!buf) { lsd_log("sfx: audio.txt missing (no footsteps, link sounds or original DreamAudio clips)"); return; }
  char *text = (char *)realloc(buf, len + 1); text[len] = 0;
  char *save = NULL;
  for (char *line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
    char a[64], b[64]; float p; int k, x, y, w, h;
    if (!strncmp(line, "clip ", 5)) { if (sscanf(line + 5, "%63s", a) == 1) sfx_clip(a); }
    else if (sscanf(line, "foot lbdfallback %63s %f", a, &p) == 2) lbd_fallback = foot_of(a, p);
    else if (sscanf(line, "foot lbd %d %63s %f", &k, a, &p) == 3) { if (k >= 0 && k < 128) lbd_foot[k] = foot_of(a, p); }
    else if (sscanf(line, "foot uvatlas %d %d", &w, &h) == 2) { uv_w = w > 0 ? w : 1024; uv_h = h > 0 ? h : 1024; }
    else if (sscanf(line, "foot uvfallback %63s %f", a, &p) == 2) uv_fallback = foot_of(a, p);
    else if (sscanf(line, "foot uv %d %d %d %d %63s %f", &x, &y, &w, &h, a, &p) == 6) {
      UVRect *n = (UVRect *)realloc(uvr, sizeof(UVRect) * (size_t)(nuvr + 1)); if (!n) continue;
      uvr = n; uvr[nuvr].x = x; uvr[nuvr].y = y; uvr[nuvr].w = w; uvr[nuvr].h = h; uvr[nuvr].f = foot_of(a, p); nuvr++;
    } else if (!strncmp(line, "link ", 5) || !strncmp(line, "fall ", 5)) {
      int isfall = line[0] == 'f', n = 0, off = 0;
      if (sscanf(line + 5, "%63s %d %n", a, &n, &off) >= 2) {
        float *dst = isfall ? fall_p : link_p; int m = 0; char *q = line + 5 + off;
        for (int i = 0; i < n && i < 8; i++) { char *e; float v = strtof(q, &e); if (e == q) break; dst[m++] = v; q = e; }
        if (isfall) { fall_clip = sfx_clip(a); nfall = m; } else { link_clip = sfx_clip(a); nlink = m; }
      }
    } else if (sscanf(line, "soundtest %63s", a) == 1) test_clip = sfx_clip(a);
    else if (sscanf(line, "musicrule original %d %d", &x, &y) == 2) { g_sfx_songs_per_dream = x > 0 ? x : 5; g_sfx_day1_song = y; }
    else if (sscanf(line, "modsongs %7s %d", b, &k) == 2 && nmodsong < 32) { snprintf(modsong_folder[nmodsong], 8, "%s", b); modsong_idx[nmodsong++] = k; }
  }
  free(text);
  size_t bytes = 0; for (int i = 0; i < ngc; i++) bytes += gc[i]->c.n * 2;
  int nl = 0; for (int i = 0; i < 128; i++) nl += lbd_foot[i].set;
  lsd_log("sfx: %d clips (%u KB), %d LBD footstep keys, %d UV footstep cells, link %s (%d pitches), sound test %s", ngc, (unsigned)(bytes / 1024),
          nl, nuvr, link_clip ? "ok" : "missing", nlink, test_clip ? "ok" : "missing");
}

int sfx_mod_songs(const char *folder) {
  for (int i = 0; i < nmodsong; i++) if (!strcmp(modsong_folder[i], folder)) return modsong_idx[i];
  if (!strncmp(folder, "ADD", 3)) return atoi(folder + 3) % 14;    /* GetDreamIndex(dream) % 14 without the table */
  return -1;
}

static void play_foot(const Foot *f) { if (f->set && f->c) audio_play2d(f->c, f->pitch, 1.0f); }

int sfx_footstep(const Level *lv, const Player *pl) {
  const CWorld *w = &lv->world;
  float g; int tri;
  /* the capsule cast down (radius 0.08, from feet + 0.1, 0.16 long): ground at most ~0.14 below the feet */
  if (!cw_ground_ex(w, pl->x, pl->z, pl->y + 0.1f, &g, &tri) || pl->y - g > 0.15f || tri < 0) return -2;
  int tag = w->tris[tri].tag;
  if (lv->mw.loaded) {                                  /* UVFootstepIndex: the texture coordinate under the feet */
    if (!lv->solid_map || tag < 0 || tag >= lv->nsolid_map) { play_foot(&uv_fallback); return -1; }
    const MWTri *t = &lv->mw.tris[lv->solid_map[tag]];
    float ax = t->p[0], az = t->p[2], bx = t->p[3], bz = t->p[5], cx = t->p[6], cz = t->p[8];
    float den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    float wa = 1.0f / 3, wb = 1.0f / 3, wc = 1.0f / 3;
    if (fabsf(den) > 1e-9f) { wa = ((bz - cz) * (pl->x - cx) + (cx - bx) * (pl->z - cz)) / den; wb = ((cz - az) * (pl->x - cx) + (ax - cx) * (pl->z - cz)) / den; wc = 1 - wa - wb; }
    float u = wa * t->uv[0] + wb * t->uv[2] + wc * t->uv[4], v = wa * t->uv[1] + wb * t->uv[3] + wc * t->uv[5];   /* v is stored top-down */
    int px = (int)(u * uv_w), py = (int)(v * uv_h);
    for (int i = 0; i < nuvr; i++) {
      const UVRect *r = &uvr[i];
      if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h) {
        play_foot(&r->f);
        if (g_foot_log) lsd_log("footstep uv %.3f,%.3f -> rect %d (%d,%d) pitch %.2f", u, v, i, r->x, r->y, r->f.pitch);
        return 1000 + i;
      }
    }
    play_foot(&uv_fallback);
    if (g_foot_log) lsd_log("footstep uv %.3f,%.3f -> fallback", u, v);
    return -1;
  }
  /* LBDFootstepIndex: the key of the main tile of the cell under the hit point, in the chunk the hit triangle belongs to */
  int ci = tag >> 16;
  if (tag < 0 || ci >= lv->n) { play_foot(&lbd_fallback); return -1; }
  const LChunk *c = &lv->chunks[ci];
  float fx = pl->x - lv->ox[ci] + 0.5f, fz = pl->z - lv->oz[ci] + 0.5f;    /* tile origin = the centre of its cell */
  int cx = (int)fx, cz = (int)fz;                                           /* C# (int): truncation */
  if (fx <= -1 || fz <= -1 || cx < 0 || cz < 0 || cx >= c->w || cz >= c->h) { play_foot(&lbd_fallback); return -1; }
  int idx = cz * c->w + cx;                         /* the app's layout: raw tile idx at x = idx % w, z = idx / w */
  int key = c->tiles[idx].footstep & 0x7F;
  const Foot *f = lbd_foot[key].set ? &lbd_foot[key] : &lbd_fallback;
  play_foot(f);
  if (g_foot_log) {
    int own = tag & 0xFFFF, tidx = cx * c->w + cz;   /* the triangle's own tile, and the transposed reading (Revamped's i = x*20+z) */
    lsd_log("footstep chunk %d cell %d,%d tile %d key %d%s (triangle of tile %d key %d; transposed tile %d key %d) pitch %.2f",
            ci, cx, cz, idx, key, lbd_foot[key].set ? "" : " (fallback)", own, own < c->w * c->h ? c->tiles[own].footstep & 0x7F : -1,
            tidx, tidx < c->w * c->h ? c->tiles[tidx].footstep & 0x7F : -1, f->pitch);
  }
  return key;
}

void sfx_link(unsigned *rng) {
  if (!link_clip || !nlink) return;
  float p = link_p[rng_next(rng) % (unsigned)nlink]; int v = audio_play2d(link_clip, p, 1.0f);
  lsd_log("link sound, pitch %.2f (voice %d)", p, v);
}
void sfx_fall(unsigned *rng) {
  if (!fall_clip || !nfall) return;
  float p = fall_p[rng_next(rng) % (unsigned)nfall]; int v = audio_play2d(fall_clip, p, 1.0f);
  lsd_log("fall sound, pitch %.2f (voice %d)", p, v);
}
void sfx_soundtest(void) {
  if (!test_clip) return;
  if (test_voice >= 0) audio_voice_stop(test_voice);
  test_voice = audio_play2d(test_clip, 1.0f, 1.0f);
}

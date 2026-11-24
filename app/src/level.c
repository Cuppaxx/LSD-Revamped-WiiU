#include "level.h"
#include "model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned lsd_ticks(void);
unsigned rng_next(unsigned *s) { unsigned x = *s ? *s : 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* TextureSetter.SetRandomTextureSetFromDayNumber: a new texture set joins the mix every 10 days */
static int pick_texset(int day, unsigned *rng) {
  int d = day % 41;
  if (d <= 10) return 0;
  if (d <= 20) return rng_next(rng) % 2;
  if (d <= 30) return rng_next(rng) % 3;
  return rng_next(rng) % 4;
}

void level_free(Level *lv) {
  for (int i = 0; i < lv->n; i++) lsd_free_chunk(&lv->chunks[i]);
  free(lv->chunks); free(lv->ox); free(lv->oz);
  lsd_free_atlas(&lv->atlas); cw_free(&lv->world); water_free(&lv->water); models_free(); mw_free(&lv->mw); free(lv->live); free(lv->solid_map);
  memset(lv, 0, sizeof *lv);
}

void level_update_live(Level *lv) {
  Dream *d = (Dream *)lv->dream; if (!d) return;
  for (int i = 0; i < d->nent; i++) {
    Ent *e = &d->ent[i]; int p = e->parent;
    e->live = e->active && (p < 0 || p >= i || d->ent[p].live);     /* parents come before their children */
    if (lv->live) lv->live[i] = (unsigned char)e->live;
  }
}

/* solid colliders of the entities (ecol lines) at their prefab placement: Unity yaw x' = x cos + z sin, z' = -x sin + z cos */
static void add_entity_colliders(Level *lv, const Dream *d) {
  int n = 0;
  for (int i = 0; i < d->nent; i++) {
    const Ent *e = &d->ent[i];
    float a = e->yaw0 * 0.0174532925f, cs = cosf(a), sn = sinf(a);
    float sx = e->scale0[0] ? fabsf(e->scale0[0]) : 1, sy = e->scale0[1] ? fabsf(e->scale0[1]) : 1, sz = e->scale0[2] ? fabsf(e->scale0[2]) : 1;
    for (int k = 0; k < e->necol; k++) {
      const ECol *c = &e->ecol[k]; if (!c->enabled) continue;
      float lx = c->c[0] * sx, ly = c->c[1] * sy, lz = c->c[2] * sz;
      CDyn dy; memset(&dy, 0, sizeof dy); dy.ent = i;
      dy.x = e->pos0[0] + lx * cs + lz * sn; dy.z = e->pos0[2] - lx * sn + lz * cs; dy.cs = cs; dy.sn = sn;
      float cy = e->pos0[1] + ly;
      if (c->type == 0) { dy.type = 0; dy.hx = c->s[0] * sx * 0.5f; dy.hz = c->s[2] * sz * 0.5f; dy.y0 = cy - c->s[1] * sy * 0.5f; dy.y1 = cy + c->s[1] * sy * 0.5f; }
      else if (c->type == 2 || (c->type == 1 && c->dir == 1)) {         /* sphere, upright capsule: a cylinder */
        float hh = c->type == 2 ? c->r * sy : fmaxf(c->h * 0.5f, c->r) * sy;
        dy.type = 1; dy.r = c->r * fmaxf(sx, sz); dy.y0 = cy - hh; dy.y1 = cy + hh;
      } else {                                                          /* lying capsule: its box */
        dy.type = 0; float ha = c->h * 0.5f;
        dy.hx = (c->dir == 0 ? ha : c->r) * sx; dy.hz = (c->dir == 2 ? ha : c->r) * sz; dy.y0 = cy - c->r * sy; dy.y1 = cy + c->r * sy;
      }
      cw_add_dyn(&lv->world, &dy); n++;
    }
  }
  if (n) lsd_log("entity colliders: %d", n);
}

/* layout 2: an Additional Dream (meshworld.c) */
static int level_load_mesh(Level *lv, const DreamList *dl, int index, int day, unsigned *rng, unsigned t0) {
  const Dream *d = &dl->d[index];
  int rc = mw_load(&lv->mw, d->world);
  if (rc) { lsd_log("%s: load failed rc=%d", d->world, rc); return -1; }
  lv->texset = pick_texset(day, rng);
  mw_set_texset(&lv->mw, lv->texset);
  const Env *e = dream_env(d, index, day, &lv->env_group); lv->env = *e;
  skyfx_build(&lv->fx, &lv->env, d->fx, lv->env_group, day, rng);
  lv->draw_dist = fminf(20.0f, lv->env.end) + 1.0f;
  unsigned t1 = lsd_ticks();
  int ns; float *solid = mw_solid_tris(&lv->mw, &ns);
  cw_build_tris(&lv->world, solid, ns); free(solid);
  lv->solid_map = (int *)malloc(sizeof(int) * ((size_t)ns + 1));       /* the k-th solid triangle (collision tag) -> mw.tris index */
  if (lv->solid_map) { int k = 0; for (int i = 0; i < lv->mw.ntris && k < ns; i++) if (lv->mw.tris[i].flags & 2) lv->solid_map[k++] = i; lv->nsolid_map = k; }
  unsigned t2 = lsd_ticks();
  dream_roll_gates((Dream *)d, day, rng);
  add_entity_colliders(lv, d);
  lv->live = (unsigned char *)calloc((size_t)d->nent + 1, 1); lv->world.live = lv->live;
  models_load_mesh(d, &lv->mw);
  lsd_log("dream %d %s: mesh world %d tris (%d drawn, %d solid) in %d chunks, texture set %c, env fog %.0f-%.0f, collision %d tris built in %u ms, loaded in %u ms",
          index, d->name, lv->mw.ntris, lv->mw.ndrawn, lv->mw.nsolid, lv->mw.nchunks, 'A' + lv->texset, lv->env.start, lv->env.end, lv->world.ntris, t2 - t1, lsd_ticks() - t0);
  return 0;
}

int level_load(Level *lv, const DreamList *dl, int index, int day, unsigned *rng) {
  level_free(lv);
  unsigned t0 = lsd_ticks();
  const Dream *d = &dl->d[index];
  lv->dream = d; lv->index = index;
  if (d->layout == 2) return level_load_mesh(lv, dl, index, day, rng, t0);
  lv->chunks = (LChunk *)calloc((size_t)d->nlbd + 1, sizeof(LChunk));
  lv->ox = (float *)calloc((size_t)d->nlbd + 1, sizeof(float)); lv->oz = (float *)calloc((size_t)d->nlbd + 1, sizeof(float));
  int width = d->tilew > 0 ? d->tilew : 1;
  for (int i = 0; i < d->nlbd; i++) {
    char path[64]; snprintf(path, sizeof path, "%s/%s", d->folder, d->lbd[i]);
    size_t n; uint8_t *buf = lsd_read_file(path, &n);
    if (!buf) { lsd_log("missing %s", path); continue; }
    int r = lsd_load_chunk(buf, n, &lv->chunks[lv->n]); free(buf);
    if (r) { lsd_log("%s: load failed rc=%d", path, r); continue; }
    if (d->layout == 0) {                                  /* horizontal: honeycomb of 20x20 chunks */
      int xp = i % width, yp = i / width, xm = (yp % 2 == 1) ? 10 : 0;
      lv->ox[lv->n] = (float)(xp * 20 - xm); lv->oz[lv->n] = (float)(yp * 20);
    }                                                      /* vertical: every chunk at the origin, stacked by tile height */
    lv->n++;
  }
  if (!lv->n) return -1;
  int nwater_tris = 0; float wxz[2] = {0, 0};
  { char wp[64]; snprintf(wp, sizeof wp, "%s/water.bin", d->folder); size_t wn; uint8_t *wb = lsd_read_file(wp, &wn);
    if (wb) { if (water_load(&lv->water, wb, wn) == 0) for (int i = 0; i < lv->n; i++) water_apply(&lv->chunks[i], &lv->water); free(wb); } }
  if (lv->water.n) {                                      /* log how much water there is and where the first water tile is */
    for (int i = 0; i < lv->n && !nwater_tris; i++) { const LChunk *c = &lv->chunks[i];
      for (int idx = 0; idx < c->w * c->h && !nwater_tris; idx++) { const LTile *t = &c->tiles[idx]; if (!t->draw || t->type >= c->nmesh) continue;
        const LMesh *m = &c->mesh[t->type]; for (int k = 0; k < m->ntris; k++) if (m->tris[k].water) { nwater_tris++; wxz[0] = idx % c->w + lv->ox[i]; wxz[1] = idx / c->w + lv->oz[i]; break; } } }
    lsd_log("water: %d materials, first water tile at %.0f,%.0f", lv->water.n, wxz[0], wxz[1]);
  }
  lv->texset = pick_texset(day, rng);
  char tix[64]; snprintf(tix, sizeof tix, "%s/TEX%c.TIX", d->folder, 'A' + lv->texset);
  size_t n; uint8_t *buf = lsd_read_file(tix, &n);
  if (!buf) { lv->texset = 0; snprintf(tix, sizeof tix, "%s/TEXA.TIX", d->folder); buf = lsd_read_file(tix, &n); }
  int rc = buf ? lsd_load_atlas(buf, n, &lv->atlas) : -100;
  free(buf);
  const Env *e = dream_env(d, index, day, &lv->env_group); lv->env = *e;
  skyfx_build(&lv->fx, &lv->env, d->fx, lv->env_group, day, rng);
  lv->draw_dist = fminf(20.0f, lv->env.end) + 1.0f;       /* DreamEnvironment.DRAW_DISTANCE, and nothing past the fog end */
  cw_build(&lv->world, lv->chunks, lv->ox, lv->oz, lv->n);
  lv->live = (unsigned char *)calloc((size_t)d->nent + 1, 1);
  models_load(d, &lv->atlas);                                        /* animated object models of the entities (models.bin) */
  lsd_log("dream %d %s: %d chunks, %s, env group %d fog %.0f-%.0f, %d collision tris, atlas rc=%d, loaded in %u ms", index, d->name, lv->n, tix, lv->env_group, lv->env.start, lv->env.end, lv->world.ntris, rc, lsd_ticks() - t0);
  return 0;
}

char g_force_spawn[48];
int g_draw_models = 1;
void level_spawn(const Level *lv, Player *p, int day, unsigned *rng) {
  const Dream *d = lv->dream;
  const Spawn *pick = NULL; int n = 0;
  if (g_force_spawn[0] && strcmp(g_force_spawn, "-")) for (int i = 0; i < d->nspawn; i++) if (!strcmp(d->spawn[i].name, g_force_spawn)) { pick = &d->spawn[i]; break; }
  g_force_spawn[0] = 0;
  if (pick) goto picked;
  for (int i = 0; i < d->nspawn; i++) if (day == 1 && d->spawn[i].dayone) { n++; if (rng_next(rng) % n == 0) pick = &d->spawn[i]; }
  if (!pick) { n = 0; for (int i = 0; i < d->nspawn; i++) if (!d->spawn[i].tunnel) { n++; if (rng_next(rng) % n == 0) pick = &d->spawn[i]; } }
  if (!pick && d->nspawn) pick = &d->spawn[rng_next(rng) % d->nspawn];
picked:
  if (pick) lsd_log("spawn %s at %.2f,%.2f,%.2f", pick->name, pick->x, pick->y, pick->z);
  memset(p, 0, sizeof *p);
  if (pick) {
    p->x = pick->x + 0.01f; p->y = pick->y; p->z = pick->z - 0.01f;
    p->yaw = pick->yaw * 0.0174532925f;
    float g;
    if (cw_ground(&lv->world, p->x, p->z, p->y + PLAYER_STEP, &g) && p->y - g < 0.3f) p->y = g;
    p->grounded = 1;
    return;
  }
  player_spawn(p, &lv->world, 20, 20);                    /* no spawn points: find open floor */
}

void level_render(Frame *f, const Cam *cam, const Level *lv, RenderStats *st) {
  if (lv->mw.loaded) {
    render_meshworld(f, cam, &lv->mw, st);
    if (g_draw_models) models_render(f, cam, lv->dream, NULL, st);
    return;
  }
  render_set_water(&lv->water, lv->texset);
  for (int i = 0; i < lv->n; i++) {
    float dx = lv->ox[i] + 10 - cam->x, dz = lv->oz[i] + 10 - cam->z;
    if (dx * dx + dz * dz > (cam->draw_dist + 15) * (cam->draw_dist + 15)) continue;   /* whole chunk out of range */
    render_chunk(f, cam, &lv->chunks[i], &lv->atlas, lv->ox[i], lv->oz[i], st);
  }
  if (g_draw_models) models_render(f, cam, lv->dream, &lv->atlas, st);
}

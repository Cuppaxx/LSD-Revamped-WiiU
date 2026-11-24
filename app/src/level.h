/* level.h - one loaded dream: its LBD chunks laid out like LBDFastMeshSystem.LoadLBD, texture set, environment, collision */
#ifndef LEVEL_H
#define LEVEL_H
#include "lsd.h"
#include "dream.h"
#include "collide.h"
#include "player.h"
#include "render.h"

typedef struct {
  const Dream *dream; int index;
  int n; LChunk *chunks; float *ox, *oz;
  float (*box)[6];               /* per chunk: world box (min xyz, max xyz) of its tiles, for the view culling */
  LAtlas atlas; int texset;
  Env env; int env_group; SkyFx fx;
  WaterSet water;
  CWorld world;
  float draw_dist;
  MeshWorld mw;                  /* layout 2 (Additional Dreams): the static mesh world instead of LBD chunks */
  unsigned char *live;           /* per entity: active and every parent entity active (level_update_live) */
  int *solid_map; int nsolid_map;   /* mesh worlds: collision triangle tag (k-th solid triangle) -> index in mw.tris (footsteps) */
} Level;
void level_update_live(Level *lv);   /* once per frame before scripts/models: Ent.live and the collision world's view of it */

unsigned rng_next(unsigned *s);
int  level_load(Level *lv, const DreamList *dl, int index, int day, unsigned *rng);   /* 0 = ok */
void level_free(Level *lv);
void level_spawn(const Level *lv, Player *p, int day, unsigned *rng);
extern char g_force_spawn[48];
extern int g_draw_models;        /* options: draw the animated object models */   /* spawn point ID for the next level_spawn (Link triggers), cleared after use */
void level_render(Frame *f, const Cam *cam, const Level *lv, RenderStats *st);

#endif

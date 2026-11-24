/* collide.h - static triangle world for the player: ground height queries and wall blocking.
 * Mirrors what Revamped gets from Unity: a MeshCollider built from every drawn tile (LBDFastMeshSystem.createLBDCollider)
 * and a CharacterController (radius 0.08, height 0.4, step 0.2, slope limit 48 degrees). */
#ifndef COLLIDE_H
#define COLLIDE_H
#include "lsd.h"

#define PLAYER_RADIUS 0.08f
#define PLAYER_HEIGHT 0.4f
#define PLAYER_STEP   0.2f
#define SLOPE_COS     0.669f      /* cos(48 deg): flatter than this is floor, steeper is wall */

/* tag: where the triangle came from: LBD worlds (chunk << 16) | raw tile index, mesh worlds the index of the solid triangle */
typedef struct { float a[3], b[3], c[3], n[3]; float minx, maxx, minz, maxz, miny, maxy; int tag; } CTri;
/* a solid collider of an entity (mesh dreams: ChangingTree capsules, hospital beds...): blocks, cannot be stood on.
 * type 0: box centred at x,z with half extents hx,hz turned by the entity yaw (cs, sn); 1: vertical cylinder radius r */
typedef struct { int ent, type; float x, z, r, hx, hz, cs, sn, y0, y1; } CDyn;
typedef struct {
  CTri *tris; int ntris;
  float x0, z0; int gw, gh;          /* grid of 1x1 cells covering the chunk */
  int *start, *idx;                  /* cell -> triangle list (CSR) */
  float miny;
  CDyn *dyn; int ndyn;               /* entity colliders */
  const unsigned char *live;         /* live[ent]: the entity is active this frame (NULL = all) */
} CWorld;

/* all chunks of a dream at their offsets (vertical layouts stack them at the same offset) */
void cw_build(CWorld *w, const LChunk *chunks, const float *ox, const float *oz, int nchunks);
/* a triangle soup (9 floats per triangle: a, b, c), mesh dreams */
void cw_build_tris(CWorld *w, const float *xyz, int n);
void cw_add_dyn(CWorld *w, const CDyn *d);
void cw_free(CWorld *w);
/* highest walkable surface at (x,z) not above ymax; returns 1 and the height */
int cw_ground(const CWorld *w, float x, float z, float ymax, float *gy);
/* the same, also reporting the triangle (index into tris) */
int cw_ground_ex(const CWorld *w, float x, float z, float ymax, float *gy, int *tri);
/* does a vertical capsule slab [y0,y1] with the given radius at (x,z) touch a wall? */
int cw_blocked(const CWorld *w, float x, float z, float y0, float y1, float r);

#endif

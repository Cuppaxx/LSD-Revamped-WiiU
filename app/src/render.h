#ifndef RENDER_H
#define RENDER_H
#include "lsd.h"
#include "raster.h"
#include "dream.h"
#include "meshworld.h"

typedef struct { float x, y, z, yaw, pitch; float focal; float draw_dist; } Cam;   /* LH, +Z forward at yaw 0 (Unity convention) */
#include "sky.h"            /* gradient sky + environment effects (uses Cam) */

typedef struct { int tris_drawn, tiles_drawn; } RenderStats;

/* draw one chunk with its origin at (ox,0,oz) in tile units */
void render_chunk(Frame *f, const Cam *cam, const LChunk *c, const LAtlas *atlas, float ox, float oz, RenderStats *st);
void render_set_water(const WaterSet *ws, int texset);   /* the current dream's water materials, call before render_chunk */
/* a free-standing mesh (e.g. the grey man) at world (x,y,z) turned by yaw about Y */
void render_mesh(Frame *f, const Cam *cam, const LMesh *m, float x, float y, float z, float yaw, RenderStats *st);
/* a mesh with a full object-to-world matrix W (row-major 3x4: world = W[0..2,0..2] * v + W[0..2,3]). atlas: the dream's
 * texture atlas pixels for textured triangles (u >= 0), NULL draws every triangle with its vertex colour */
void render_mesh_xf(Frame *f, const Cam *cam, const LMesh *m, const float W[12], const uint32_t *atlas, RenderStats *st);
void cam_basis(const Cam *cam, float right[3], float up[3], float fwd[3]);   /* camera-space axes in world space */
/* mesh dreams: the static world (chunk box vs view distance and frustum, then per triangle) and model meshes whose
 * triangles carry world.bin material indices (LTri.mat, u,v 0..1) */
void render_meshworld(Frame *f, const Cam *cam, const MeshWorld *mw, RenderStats *st);
void render_mesh_mw(Frame *f, const Cam *cam, const LMesh *m, const float W[12], const MeshWorld *mw, RenderStats *st);
void chunk_bounds(const LChunk *c, float ox, float oz, float min[3], float max[3]);
void env_apply_fog(Frame *f, const Env *e);
void draw_text(Frame *f, int x, int y, const char *s, uint32_t argb);
extern float g_snap_step;   /* vertex snap grid in pixels, 0 = off */
void render_set_affine(float intensity);   /* texture warping 0 (perspective-correct) .. 1 (PS1 affine), default 0.5 */

#endif

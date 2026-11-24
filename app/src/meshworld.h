/* meshworld.h - "mesh dreams" (layout 2, the Additional Dreams mod): static triangle soup in chunks with bounding boxes,
 * per-triangle materials and textures of arbitrary size, baked by tools/addl.py into ADDnn/world.bin (format:
 * tools/ADDL_FORMAT.md section 3) plus ADDTEX/NAME.tex textures (raw RGBA8, section 5).
 *
 * Space is Unity's (left-handed, +Y up, 1 unit = 1 tile), the same as the original dreams after level.c's layout. */
#ifndef MESHWORLD_H
#define MESHWORLD_H
#include <stdint.h>
#include "lsd.h"
#include "raster.h"

typedef struct { char name[64]; uint32_t *px; int w, h; } MTexture;    /* ARGB8888 like the VRAM atlas; px NULL = missing */

typedef struct {
  uint8_t shader;                 /* 0 cutout, 1 alpha blend, 2 water */
  uint8_t cull;                   /* 0 double-sided, 2 back faces culled */
  int16_t tex[4], pal[4];         /* texture per texture set A..D (-1 white), water palette per set */
  float alpha, speed;             /* water: _Alpha, _AnimationSpeed */
  char name[40];
  RTex rt;                        /* what the rasterizer samples for the current texture set (mw_set_texset) */
} MMat;

typedef struct { float p[9]; float uv[6]; uint8_t c[12]; uint16_t mat; uint8_t flags, pad; } MWTri;   /* flags: 1 drawn, 2 solid */
/* drawn triangles first. ftris/nfar: the drawn triangles as baked, before the load-time split of big ones (render.c draws
 * those for chunks past the classic view, where the split buys nothing); NULL = nothing was split, use tris */
typedef struct { int cx, cz; float mn[3], mx[3]; int ntris, ndrawn; MWTri *tris; MWTri *ftris; int nfar; } MChunk;

typedef struct {
  int loaded; float cell;
  int ntex; MTexture **tex;       /* entries of the global texture cache (shared by all mesh dreams, kept loaded) */
  int nmat; MMat *mat;
  int nchunks; MChunk *chunks; MWTri *tris; int ntris, ndrawn, nsolid;
  MWTri *ftris; int nfar;         /* the unsplit drawn triangles of the chunks that had splits (MChunk.ftris points in here) */
  float bmin[3], bmax[3];
  int texset;
} MeshWorld;

int  mw_load(MeshWorld *mw, const char *path);          /* 0 = ok */
void mw_free(MeshWorld *mw);
void mw_set_texset(MeshWorld *mw, int set);             /* 0..3: picks tex[set] of every material (loads the textures) */
/* the solid triangles as 9 floats each (malloc'd), for cw_build_tris */
float *mw_solid_tris(const MeshWorld *mw, int *n);
const RTex *mw_rtex(const MeshWorld *mw, int mat);      /* NULL for an invalid index */

#endif

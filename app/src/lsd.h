/* lsd.h - loaders for the original LSD: Dream Emulator PS1 data (LBD level chunks, TIX texture archives).
 * A C port of the parts of Figglewatts/libLSD (MIT) that LSD: Revamped uses, plus the mesh/atlas conversion from
 * Revamped's LibLSDUnity.cs. All file data is little-endian; the Wii U CPU is big-endian, so every read goes through rd16/rd32. */
#ifndef LSD_H
#define LSD_H
#include <stdint.h>
#include <stddef.h>

#define VRAM_W 2056
#define VRAM_H 512

typedef struct { float x, y, z; float u, v; uint8_t r, g, b, a; } LVtx;       /* u,v = texel position in the VRAM atlas */
typedef struct { LVtx v[3]; uint8_t alpha_blend; uint8_t water; uint16_t mat; } LTri;   /* mat: material of a mesh dream model (amodels.bin; u,v are then 0..1 texture coordinates) */   /* water: 0 = normal, else 1 + water material index (u,v are then water-map texels) */                       /* one triangle, three corners with their own colour/uv */

typedef struct { LTri *tris; int ntris; } LMesh;                               /* one TMD object (one tile shape) */

typedef struct { uint8_t draw, solid, footstep, collision, dir; int16_t height; int extra; uint16_t type; } LTile;

typedef struct {
  int w, h;               /* tile grid size (20x20) */
  LTile *tiles;           /* w*h */
  int nextra; LTile *extra;
  int nmesh; LMesh *mesh; /* tile shapes, indexed by LTile.type */
} LChunk;

/* 2056x512 ARGB8888 atlas painted from every TIM in the TIX, like LibLSDUnity.GetTextureFromTIX (CLUT 0). a=0 means cutout */
typedef struct { uint32_t *px; } LAtlas;

uint16_t rd16(const uint8_t *p);
uint32_t rd32(const uint8_t *p);

/* water: palette-cycled materials picked by UV rectangles in the atlas (UVMaterialOverride + LSDR/ClassicWater), see tools/convert.py */
typedef struct { float rect[4], alpha, speed; int mw, mh, pw; const uint8_t *ra; const uint8_t *pal[4]; } WaterTex;   /* ra = (red index, alpha) per texel; pal = RGB x pw, one per texture set */
typedef struct { int n; WaterTex *t; uint8_t *blob; } WaterSet;
int  water_load(WaterSet *ws, const uint8_t *data, size_t len);
void water_free(WaterSet *ws);
void water_apply(LChunk *c, const WaterSet *ws);

/* whole-file reader supplied by the platform (main.c); returns malloc'd buffer or NULL */
uint8_t *lsd_read_file(const char *rel, size_t *len);

int  lsd_load_chunk(const uint8_t *lbd, size_t len, LChunk *out);   /* 0 = ok */
void lsd_free_chunk(LChunk *c);
int  lsd_load_mom_mesh(const uint8_t *mom, size_t len, LMesh *out);   /* first TMD object of a MOM as a mesh, 0 = ok */
int  lsd_load_atlas(const uint8_t *tix, size_t len, LAtlas *out);   /* 0 = ok */
void lsd_free_atlas(LAtlas *a);
void lsd_log(const char *fmt, ...);
uint32_t *lsd_decode_tim(const uint8_t *tim, size_t len, int *w, int *h);   /* whole TIM as malloc'd ARGB8888 (front end art), NULL on error */

#endif

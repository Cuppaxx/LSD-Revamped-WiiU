/* model.h - the visuals of entities: animated MOM models (TMD meshes + TOD animations) and loose MOM meshes, baked by
 * tools/models.py into <dream folder>/models.bin (layout in model.c and tools/MODELS_FORMAT.md) and attached to entities
 * by the `model` / `mpose` / `mmesh` lines of dreams.txt (Ent.parts).
 *
 * Unity conventions throughout: left-handed, Y up, +Z forward, column vectors (p' = T * R * S * p), Unity quaternions
 * (x, y, z, w); the yaw quaternion (0, sin(a/2), 0, cos(a/2)) maps x' = x*cos a + z*sin a, z' = -x*sin a + z*cos a like
 * the level code. World matrix of a model object:
 *   entity TRS (position, rotation, scale; owned by Lua, pushed with model_set_xform)
 *   * part TRS (where the model root sits in the entity frame) * parent objects' local TRS * the object's local TRS
 * Animations are stepped (an object holds its last key at or before the current frame), 20 frames per second, looping;
 * objects without keys in the playing clip keep their initial pose. Before Play (or AutoPlay) the model is in its rest pose,
 * like the importer's Animator whose default state is an empty "idle". */
#ifndef MODEL_H
#define MODEL_H
#include "lsd.h"
#include "render.h"
#include "dream.h"

#define MODEL_FPS 20.0f

typedef struct { float p[3], q[4], s[3]; } MTrs;                 /* local position, rotation (x,y,z,w), scale */
typedef struct { int frame; MTrs t; } MKey;
typedef struct { int obj; int nkeys; MKey *keys; } MTrack;        /* keys of one object (obj = object index), sorted by frame */
typedef struct { int nframes, length; int ntracks; MTrack *tracks; int *track_of; float fps; int loop; char name[32]; } MClip;   /* length in frames (clip = length / fps s); track_of[obj] = track or -1; loop 0 = hold the last frame */
typedef struct {
  int id, parent;                 /* parent = object index (-1 = the model root, i.e. the part's transform) */
  char name[32]; char path[96];   /* GameObject name and its path below the model root ("0/3") */
  MTrs pose;                      /* initial local pose */
  int nmesh; int *mesh;           /* indices into Model.mesh */
} MObj;
typedef struct {
  char key[64];                   /* "<lbd path relative to the dream folder>#<mom index>" */
  int nmesh; LMesh *mesh;         /* the MOM's TMD objects */
  int nobj; MObj *obj;            /* parents before children */
  int nclip; MClip *clip;         /* AnimatedObject.Clips */
  float radius;                   /* rough bounding radius of the rest pose around the model root */
  int ntris;
} Model;

/* load <folder>/models.bin for this dream and attach the models to its entities (Ent.parts). Called by level_load after
 * the atlas (used to drop fully transparent triangles); frees the previous dream's models. 0 = ok (also without models). */
int  models_load(const Dream *d, const LAtlas *atlas);
/* mesh dreams: <models> (amodels.bin, tools/ADDL_FORMAT.md section 4: named clips at their own fps, triangles with world.bin
 * materials) drawn with the materials of mw */
int  models_load_mesh(const Dream *d, const MeshWorld *mw);
void models_free(void);
void models_update(float dt, const Dream *d);                    /* advance the animation clocks */
/* draw the visuals of active entities within the camera's draw distance and view */
void models_render(Frame *f, const Cam *cam, const Dream *d, const LAtlas *atlas, RenderStats *st);
void models_stats(int *nent, int *ndrawn, int *ntris);           /* last frame: entities with visuals, drawn, triangles */

/* script hooks. ent = 0-based entity index. path = a GameObject path below the entity as Transform.Find takes it (a part,
 * a model object, or an empty GameObject above them); NULL means the entity's own AnimatedObject / first child. */
int   model_has(int ent);
void  model_set_xform(int ent, const float p[3], const float q[4], const float s[3]);
void  model_play(int ent, const char *path, int clip);          /* AnimatedObject.Play: restart the clip */
void  model_stop(int ent, const char *path);                    /* AnimatedObject.Stop: freeze (Animator disabled) */
void  model_resume(int ent, const char *path);                  /* AnimatedObject.Resume */
float model_clip_seconds(int ent, const char *path, int clip);  /* -1: no such model/clip */
int   model_find(int ent, const char *path);                    /* 1 if the path names something of this entity's visuals */
void  model_set_active(int ent, const char *path, int active);  /* GameObject.SetActive below the entity (NULL: first child) */
void  model_set_renderers(int ent, int on);                     /* SetRenderersActive */
void  model_set_local_pos(int ent, const char *path, const float p[3]);   /* GameObject.LocalPosition of a part or object */

#endif

/* dream.h - the dream table produced by tools/convert.py (data/dreams.txt) */
#ifndef DREAM_H
#define DREAM_H

typedef struct {
  float fog[3]; int sub; float start, end, fogheight, foggrad; float sky[3], skyfog[3];
  /* sky effects (DreamEnvironment): suns, sunburst, clouds, stars */
  float sun[3], sun_chance, sun_height, sun_size;
  float sun2[3], sun2_size, sun2_chance, sun2_offset, sun2_height;
  float burst[3], burst_size, burst_chance;
  float cloud[3], nclouds, cloud_chance;
  float star[3], nstars, star_chance;
} Env;
typedef struct { float x, y, z, yaw; int dayone, tunnel; char name[48]; } Spawn;
/* entities from the dream prefab: kind T target, L lua trigger, K link trigger, P teleport trigger, I interactive object */
typedef struct {
  char kind; char name[48]; int active; float pos[3], yaw, scale[3], box[6];
  char script[56], fn[40], fnexit[40]; int onlyonce, fired, inside;
  int dream; char spawn[48]; int active0; int lockinput, affects;
  float dest[3]; int alwayson; int ikind; float idist; int interacted;
  float pos0[3], yaw0, scale0[3];   /* the prefab transform; scripts move pos/yaw/scale, script_level restores them */
  /* visuals (tools/models.py, see tools/MODELS_FORMAT.md): the ent line ends with "<first key> <nmodels> <nmeshes> <child0>" and is
   * followed by its `model` (+ `mpose`) and `mmesh` lines. model = first key ("" = none); child0 = name of the entity's first
   * child GameObject (SetChildVisible) */
  char model[64]; char child0[48];
  int nparts; struct EntPart *parts;
  /* Additional Dreams lines (tools/ADDL_FORMAT.md section 2); all optional, zero/-1 for the original dreams */
  int parent; char epath[48];       /* epath: nested inside entity `parent` (index, -1 none) at this child path */
  int ngates; short gates[8];       /* gates: indices into Dream.gate; all must pass (rolled once per dream load) */
  int gate_ok;                      /* 1 = every gate passed this visit (the entity may be active) */
  int startonenable;                /* InteractiveObject.CallStartOnEnable */
  char aobj[96];                    /* the AnimatedObject of this.PlayAnimation lives on this model path */
  int has_gc, gc_done; float gc[3]; /* GraphContributor: dynamic, upper, radius; logged once */
  struct AudioDef *aud;             /* DreamAudio fields (NULL none) */
  int necol; struct ECol *ecol;     /* solid colliders of the entity (entity-local) */
  int live;                         /* active, and every parent entity active too (level_update_live, each frame) */
  /* audio lines (tools/AUDIO_FORMAT.md section 3) */
  int linksound;                    /* K: TriggerLink.PlayLinkSound */
  int aparent; float aoff[3];       /* DreamAudio nested below entity aparent (-1 none) at local offset aoff: the sound follows it */
  char aparent_name[48];            /* (resolved to aparent after the file is read) */
  float rot[4];                     /* runtime rotation quaternion xyzw (scripts; set with the transform), for aparent offsets */
} Ent;

/* DreamAudio (SDK DreamAudio.cs): a clip played at the entity, positional with linear roll-off */
typedef struct AudioDef {
  char clip[48]; int spatial; float full, range, pitch, vol, delay, playfor, delaybefore; int waitaudible, loop, oneshot, scripted;
} AudioDef;
/* a solid collider of an entity: 0 box (c = centre, s = size), 1 capsule (c, r, h, dir 0 X 1 Y 2 Z), 2 sphere (c, r) */
typedef struct ECol { int type, enabled, dir; float c[3], s[3], r, h; } ECol;
typedef struct { int one_in, daymask; } Gate;   /* ChanceToAppear (1 in one_in) and AppearOnDay (DayKind flags, -1 none) */

/* one piece of an entity's visuals: an animated model instance (a MOM root GameObject with an AnimatedObject) or a loose
 * mesh GameObject, at a path below the entity. t = local TRS relative to the entity frame T(pos) * R(yaw) * S(scale):
 * pos xyz, quat xyzw, scale xyz. Models can carry pose/active overrides of their objects from the prefab copy. */
typedef struct { char name[32]; int active; float t[10]; } EntPose;
typedef struct EntPart {
  int is_mesh;                      /* 0: model instance (key), 1: loose mesh (mesh table entry tmd of the model key) */
  char key[64]; int tmd; char path[96]; int autoplay, active; float t[10];
  int npose; EntPose *pose;
  int nclipmap; short *clipmap;     /* mclips: AnimatedObject.Clips[k] -> animation index of the model (-1 none); 0 = identity */
} EntPart;

typedef struct {
  char folder[16], name[48];
  float walk; int linkable; float weight; int firstday, layout /* 0 horizontal 1 vertical */, tilew, envoffset;
  int nlbd; char (*lbd)[24];
  int grouped;                   /* 1: four environment groups (day, pale, night, dark skies); 0: one plain list in group 0 */
  int nenv[4]; Env *env[4];
  int nspawn; Spawn *spawn;
  int nent; Ent *ent;
  int nsong[7]; char (*song[7])[64];   /* song lists per SongStyle (standard, ambient, lovely, human, electro, ethnova, cartoon) */
  int fx;                        /* HasEnvironmentEffects */
  int grey;                      /* GreyMan: this dream can spawn the grey man */
  float length;                  /* DreamLengthSeconds: how long the day lasts when it starts here (0 = random 60..360) */
  int nlgraph; signed char (*lgraph)[2];   /* per-chunk (dynamic, upper) graph contribution */
  /* layout 2 (mesh dream, Additional Dreams mod): */
  int set;                       /* 0 = original dreams (dreams.txt), 1 = Additional Dreams (dreams_addl.txt) */
  char world[48], models[48];    /* ADDnn/world.bin, ADDnn/amodels.bin */
  int ngate; Gate *gate;
} Dream;

#define GRAPH_SIZE 19
#define DREAM_SETS 2
typedef struct {
  Dream *d; int n;
  int graph[GRAPH_SIZE * GRAPH_SIZE]; int has_graph;      /* the original GraphSpawnMap (global dream indices) */
  int norig;                     /* dreams 0..norig-1 are the originals, norig..n-1 the Additional Dreams */
  int graph2[GRAPH_SIZE * GRAPH_SIZE]; int has_graph2;    /* the mod's GraphSpawnMap (global dream indices, -1 none) */
  char luainc[DREAM_SETS][48];   /* directory of `require` per set (LuaInclude, ADDLUA/include) */
} DreamList;

int  dreams_load(DreamList *l);                         /* 0 = ok; reads dreams.txt and, if present, dreams_addl.txt */
/* gates of an Additional Dream: roll ChanceToAppear/AppearOnDay once for this visit and set Ent.gate_ok */
void dream_roll_gates(Dream *d, int day, unsigned *rng);
/* is dream i part of the game with the current options (Additional Dreams on/off)? */
int  dream_enabled(const DreamList *l, int i);
extern int g_addl_enabled;                               /* options: ADDITIONAL DREAMS on/off */
/* Revamped's Dream.ChooseEnvironment(day, dreamIndex): picks the environment and reports its group (-1 = none) */
const Env *dream_env(const Dream *d, int dreamIndex, int day, int *group);
const Env *default_env(void);

#endif

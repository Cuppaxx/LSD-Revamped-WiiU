/* save.h - options and journal progress, kept in SD:/wiiu/apps/lsdr/save.txt (a small "key value" text file) */
#ifndef SAVE_H
#define SAVE_H

#define SAVE_MAX_DREAMS 32
#define SAVE_MAX_HISTORY 365          /* graph positions of the finished days (the dream chart) */
#define SAVE_NBIND 8                  /* rebindable actions (input.h ACT_N) */
#define SAVE_MAX_EC 4000              /* entity contributions kept (oldest dropped) */

typedef struct {
  int look_sens;        /* 1..10, 5 = the original rate */
  int invert_y;         /* 0/1 */
  int head_bob;         /* 0/1 */
  int run_mode;         /* 0 hold B to run, 1 B toggles running */
  int music_vol;        /* 0..10 */
  int sfx_vol;          /* 0..10 */
  /* graphics */
  int threads;          /* rasterizer threads 1..4 */
  int draw_dist;        /* render distance in tiles (20 = the original DRAW_DISTANCE); the fog end still limits it while fog is on */
  int res;              /* internal resolution: 0 240 lines (classic), 1 300, 2 360, 3 480 */
  int aspect;           /* 0 4:3, 1 16:9 (wider internal frame, same vertical field of view) */
  int scaling;          /* 0 fit (letterbox, keeps the aspect), 1 integer (pixel perfect), 2 stretch to the whole screen */
  int fog, skyfx, snap, models, show_fps;   /* toggles */
  int affine;           /* texture warping 0..4 = AffineIntensity 0, 0.25, 0.5 (default), 0.75, 1 */
  int addl;             /* the Additional Dreams mod's 14 dreams take part (links, graph, dream select); default on */
  /* controls (M9) */
  int scheme;           /* 0 FPS (Revamped's "Revamped" scheme, default), 1 TANK (the "Classic" scheme: steps, turning, look buttons) */
  int deadzone;         /* stick deadzone in 5 % steps, 0..8 (default 3 = 15 %) */
  int bind[SAVE_NBIND]; /* logical button per action (input.h ACT_*), -1 none */
  int footsteps;        /* EnableFootstepSounds, default on */
  /* special days, movies (special.c) */
  int specialdays;      /* opt_specialdays: play the special days (SettingsSystem SpecialDaysEnabled), default on */
  int intro;            /* opt_intro: the random OPENING movie at boot, default on */
} Options;

#define SAVE_MAX_SP 256               /* special days seen (journal log), oldest dropped */
typedef struct { short year, day; char kind; char asset[7]; } SaveSP;   /* kind 'v' video, 't' text; asset "01A" */

/* one entity graph contribution (DreamSequence entity list, tools/SPECIAL_FORMAT.md section 10): the flashbacks' pool */
typedef struct { short day; signed char d, u; short dream; float x, y, z, yaw; char id[48]; } SaveEC;

typedef struct {
  int version;
  int day;              /* the day the next dream belongs to (1..365) */
  int graph_x, graph_y; /* graph position of the last finished day (picks the next day's first dream) */
  int last_dream;       /* the dream played last (shown on the title screen), -1 none */
  int has_game;         /* a journal has been started (CONTINUE is offered) */
  unsigned char visited[SAVE_MAX_DREAMS];
  int nhist; signed char hist[SAVE_MAX_HISTORY][2];   /* oldest first */
  Options opt;
  int nec; SaveEC ec[SAVE_MAX_EC];   /* entity contributions of the finished days (ec lines), oldest first */
  int year;             /* JournalSaveData.YearNumber: +1 each time day 365 wraps to day 1 */
  int seenface;         /* JournalSaveData.SeenChartFace: the chart face videos have played */
  int nsp; SaveSP sp[SAVE_MAX_SP];   /* special days seen (spday lines), oldest first */
} SaveData;

extern SaveData g_save;
#define g_opt (g_save.opt)

void save_defaults(SaveData *s);
/* base = the SD card mount ("fs:/vol/external01" style); NULL: saving is off. Returns 0 if a save was read. */
int  save_init(const char *base);
int  save_write(void);                                  /* 0 = written */
int  save_serialize(const SaveData *s, char *out, int cap);   /* text; returns length or -1 */
int  save_parse(SaveData *s, const char *text);       /* fills s (starting from defaults); 0 = ok */
void save_add_history(int gx, int gy);
/* an entity contribution of the day that just ended (call after save_add_history: it belongs to the newest hist entry) */
void save_add_ec(int d, int u, float x, float y, float z, float yawdeg, int dream, const char *id);
void save_add_sp(int year, int day, char kind, const char *asset);   /* a special day was played (journal log) */
const char *save_path(void);                           /* NULL when saving is off */
void save_disable(void);                               /* stop writing (test runs) */

#endif

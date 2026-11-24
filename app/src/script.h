/* script.h - Lua 5.2 host for the dream scripts (triggers, interactive objects) and the entity layer around them */
#ifndef SCRIPT_H
#define SCRIPT_H
#include "dream.h"
#include "collide.h"
#include "player.h"

typedef struct {
  int override, forward;                 /* scripted player movement: ignore sticks, walk forward */
  int has_rot; float rot_x, rot_z;       /* turn towards this point */
  int can_control, gravity, teleports_armed;
  int interact_pressed;                  /* A pressed since the last script frame (Input.Interact.WasPressed) */
} ScriptCtl;

typedef struct {                          /* what the game loop provides to scripts */
  void (*transition)(int dream);          /* link now (-1: pick like a wall link) */
  void (*set_next)(int dream);
  void (*end_day)(void);
  void (*log_graph)(int d, int u, const char *entity);   /* an ENTITY graph contribution (LogGraphContribution) */
  int  (*day)(void);
  const char *(*zone)(void);
} ScriptHost;

void script_init(const ScriptHost *h);
int  script_level(Dream *d, const DreamList *dl, const CWorld *w);      /* (re)start scripts for a freshly loaded dream; 0 = ok */
void script_frame(float dt, Dream *d, Player *pl, const CWorld *w);     /* triggers, interaction, Lua tick */
void script_shutdown(void);
extern ScriptCtl g_sctl;

/* triggered by link entities: where does the player go? set by script_frame, consumed by the game loop */
extern int g_link_request; extern char g_link_spawn[48]; extern int g_link_lock; extern int g_link_sound;
/* DreamSystem.SetTransitionSounds: the next TransitionToDream plays the link sound (reset to 1 after each) */
extern int g_trans_sounds;
/* DreamSystem.SetNextTransitionSpawnID / SetNextTransitionColor / Flash from scripts, consumed by the game loop */
extern char g_next_spawn[48]; extern float g_next_rgb[3]; extern int g_has_next_rgb;
extern float g_flash_rgb[3], g_flash_secs; extern int g_flash_req;

/* DreamAudio (dreamaudio.c): positional clips of the audio entities (original and mesh dreams) */
void da_level(const Dream *d);                          /* load the clips this dream uses, reset the players */
void da_frame(const Dream *d, const Player *pl, const Cam *listener, float dt);
void da_play(int ent); void da_stop(int ent);           /* DreamAudio.Play / Stop from Lua (0-based entity) */
float da_get(int ent, int which);                       /* DreamAudio.Volume (0) / Pitch (1) fields */
void da_set(int ent, int which, float v);               /* (unclamped; clamped 0..1 when the next Play uses them) */
int  da_sounding(void);                                 /* sources sounding (audible or virtual) */
void da_free(void);
int  da_voices(void);                                   /* voices sounding now (diagnostics) */
void da_silence(void);                                 /* pause menu: mute the voices (the clocks stop with da_frame) */

#endif

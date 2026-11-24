/* special.h - the full-screen sequences of tools/SPECIAL_FORMAT.md: boot intro movie, special days (video or text),
 * movies started by dream objects (Lua VideoClip.Play), the day 365 ending, the chart face videos, plus the credits roll
 * and the journal screen (journal.c). Data: specials.txt, SPDAY/ TIMs, CREDITS/ (bundled or on the SD), MOVIE/ (SD only).
 *
 * While sp_active(), main.c hands the frame to sp_frame() (a 320x240 frame) instead of drawing the game or the menus, and
 * acts on its return code. Every sequence draws its own fades (Revamped's ToriiFader) and ends on a black screen when it
 * returns SPR_TITLE / SPR_DAY_DONE / SPR_EVENT_DONE. */
#ifndef SPECIAL_H
#define SPECIAL_H
#include <stdint.h>
#include "raster.h"
#include "dream.h"

enum { SPR_NONE,
       SPR_TITLE,        /* go to the title (screen is black: fade the title in over 1 s, start the title music) */
       SPR_DAY_DONE,     /* a special day ended: count it as a finished day at chart (0,0), day+1, then the next day */
       SPR_EVENT_DONE,   /* a dream object's movie ended: EndDream() (day end; in a flashback: back to the title) */
       SPR_BACK };       /* credits / journal closed: back to the menu screen that opened it */

typedef struct { int skip, back, start, up, down, left, right; } SpInput;   /* this frame: A or + (skip), B, + alone, d-pad/stick edges */

/* fader blend modes (ToriiFader + FadeBlend.shader) */
enum { BLEND_ALPHA, BLEND_ADD, BLEND_SUB };
/* the full-screen fade at alpha a (0..1) with colour rgb (0..1): Alpha dst*(1-a)+c*a, Add dst+c*a, Subtract dst-(1-c)*a */
void sp_fade_apply(Frame *f, float a, const float rgb[3], int blend);
/* DreamSystem.endDream(timedOut) random ending: RandUtil.Int(4) -> colour, fade seconds (0 = instant), blend */
void sp_random_ending(unsigned *rng, float rgb[3], float *secs, int *blend);

void sp_init(void);                          /* reads specials.txt (missing: no special days, intro, ending ...) */
int  sp_active(void);
int  sp_overlay(void);                       /* the menu is drawn underneath this frame (chart face highlight) */
int  sp_frame(Frame *f, float dt, const SpInput *in);
int  sp_menu_frame(void);                     /* 1 = journal / credits: draw into the menu frame (its width follows the aspect) */

int  sp_start_intro(unsigned *rng);          /* one random OPENING movie (only ones present on the SD); 0 = none: go to the title */
int  sp_is_special_day(int day);             /* a special day that plays with the current options (opt_specialdays / Always) */
int  sp_start_special_day(int day, unsigned *rng);   /* 1 = started (the screen is black now) */
int  sp_start_ending(void);                  /* the ENDING movie; 1 = started (0: missing, go to the title) */
/* chart opened (title or pause): face complete and not seen -> highlight, set seenface, save, play the 20 movies.
 * Returns 1 if it started (main keeps drawing the chart under it). */
int  sp_chart_opened(void);
void sp_start_credits(void);
/* journal screen. in_game: opened from the pause menu (day = the day in progress, cur_dream) */
void sp_start_journal(const DreamList *dl, int in_game, int day, int cur_dream);

/* Lua GetEntity(id).VideoClip.Play(colour): dream folder ("STG04") + entity ID -> movieclip table */
void sp_request_event(const char *folder, const char *entity, float r, float g, float b);
int  sp_event_pending(float rgb[3]);         /* 1 once per request: main fades to rgb over 1 s, then calls sp_start_event */
int  sp_start_event(void);                   /* 1 = started (0: no such movie: end the dream at once) */
extern int g_sp_test_choice;                 /* test hook: start.txt "spchoice N" forces choice N of a special day (-1 = random) */

#endif

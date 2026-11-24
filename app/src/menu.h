/* menu.h - the front end: title screen + main menu, dream select, dream chart, options (incl. graphics), pause menu.
 * Menus draw into a 240-line frame, 320 wide (4:3) or 426 wide (16:9, menu_frame_w), whatever the game's resolution;
 * every screen is laid out from the frame's width. */
#ifndef MENU_H_INCLUDED
#define MENU_H_INCLUDED
#include <stdint.h>
#include "raster.h"
#include "dream.h"

#define MENU_W 320
#define MENU_H 240

enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_START };
enum {
  MA_NONE,
  MA_CONTINUE,       /* title: resume the journal at the saved day */
  MA_NEW_GAME,       /* title: day 1, first dream */
  MA_DREAM,          /* *arg = dream index (title: start a day there; pause: go there now) */
  MA_RESUME,         /* pause: back to the game */
  MA_TITLE,          /* pause: give up this day and go back to the title */
  MA_QUIT,           /* title: leave the app */
  MA_SAVE_QUIT,      /* pause: save and leave the app */
  MA_OPTIONS,        /* an option changed: apply it now */
  MA_OPTIONS_DONE,   /* left the options screens: save them */
  MA_CHART_OPEN,     /* the dream chart was opened (main: sp_chart_opened, the chart face) */
  MA_FLASHBACK,      /* title: FLASHBACK (flashback.c) */
  MA_JOURNAL,        /* title / pause: the journal screen (sp_start_journal) */
  MA_CREDITS,        /* options: the credits roll (sp_start_credits) */
};

typedef struct {
  const DreamList *dl;
  int in_game;                 /* 1 = pause menu over a running dream */
  int day, cur_dream;          /* day of the dream in progress (pause) */
  int cur_has, cur_gx, cur_gy; /* graph position of the day in progress (pause), shown on the chart */
  const char *song;            /* music now playing, NULL none */
  const uint32_t *bg;          /* pause: the game frame (bg_w x 240, dimmed by the menu), NULL otherwise */
  int bg_w;                    /* width of bg (the menu frame's width when it was taken) */
} MenuCtx;

#define MENU_MAXI 48           /* items on one screen */
typedef struct { short x, y, w, h, item, ctl; } MenuHit;   /* touch: where an item was drawn (ctl: x where its value control starts, 0 none) */

typedef struct {
  int screen, stack[8], depth;
  int sel[16];
  float t, rep_t; int rep_dir;
  int quit_armed;
  int capture; float cap_t;    /* OPTIONS > CONTROLS: waiting for a button for action capture-1 (0 = not), seconds left */
  /* animation (menu.c): screen slide/fade, eased selection bar, eased scroll, eased row positions */
  float tr; int tr_dir;        /* 0..1 since the last screen change, direction +1 push / -1 pop */
  int snap;                    /* 1 = jump the eased values to their targets on the next draw */
  float bar_y, scroll[2], iy[MENU_MAXI];
  MenuHit hit[MENU_MAXI]; int nhit;   /* filled by menu_draw */
} Menu;

void menu_init(void);                                   /* loads the art (ETC/TITLE.TIM, ETC/HGRAPH.TIM) and sounds */
void menu_open(Menu *m, int pause);                     /* title (0) or pause (1) */
int  menu_nav(Menu *m, int nav, const MenuCtx *c, int *arg);              /* one button press -> MA_* */
int  menu_hold(Menu *m, int dir, float dt, const MenuCtx *c, int *arg);   /* held direction (NAV_UP..NAV_RIGHT, -1 none): auto-repeat */
void menu_draw(Menu *m, Frame *f, const MenuCtx *c, float dt);
int  menu_capturing(const Menu *m);                     /* a "press a button" prompt is open: send raw buttons to menu_capture */
int  menu_capture(Menu *m, int button);                 /* binds the button (logical, input.h) -> MA_OPTIONS */
/* GamePad touch: a tap at (x, y) in menu frame pixels (the frame last drawn): selects / activates / changes an item */
int  menu_tap(Menu *m, int x, int y, const MenuCtx *c, int *arg);
/* the menu look, shared by the journal and the credits (journal.c): navy starfield and the header with the gold faces */
void menu_backdrop(Frame *f, float t);
void menu_header(Frame *f, const char *title, float t);
/* the menu frame's width for the aspect option: 320 (4:3) or 426 (16:9); always 240 lines (scaled to the TV) */
int  menu_frame_w(int aspect);

/* text in the 5x7 menu font (upper case, ASCII 32..95; lower case is drawn as upper case), scale = pixel size, with a shadow */
void ui_text(Frame *f, int x, int y, const char *s, uint32_t argb, int scale);
int  ui_text_w(const char *s, int scale);
void ui_rect(Frame *f, int x, int y, int w, int h, uint32_t argb, int a256);   /* blended filled rectangle */
/* the boot splash ("PORTED BY CUPPAX", splash.c): the frame at time t seconds (320x240 frame); lasts splash_length() seconds
 * and ends in white */
void  splash_draw(Frame *f, float t);
float splash_length(void);
void  splash_chime(void);
/* the menu music for this day number (Revamped UIMainMenuMusic), a data-relative path */
const char *menu_music_for_day(int day);

#endif

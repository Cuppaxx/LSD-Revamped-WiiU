/* journal.h - the JOURNAL screen (title and pause menus) and the credits roll (OPTIONS > CREDITS), both 320x240 in the
 * menu style (menu.c's 5x7 font, navy background, face icons). Driven by special.c (sp_start_journal / sp_start_credits).
 *
 * The journal gathers what Revamped shows about the dream journal (title: "Day NNN" in the colour of day % 41, one icon per
 * year or "xN" past 10 years, the journal's name; the dream chart) plus what the save knows: days charted, visited dreams,
 * the special days seen (spday log), entity contributions (the flashback pool) and whether FLASHBACK is available.
 * Pages: OVERVIEW, DREAMS, SPECIAL DAYS, CHART (left/right or L/R switch pages, up/down scroll, B closes). */
#ifndef JOURNAL_H
#define JOURNAL_H
#include "raster.h"
#include "dream.h"
#include "special.h"

void journal_open(const DreamList *dl, int in_game, int day, int cur_dream);
int  journal_frame(Frame *f, float dt, const SpInput *in);     /* 1 = closed */

void credits_open(void);
int  credits_frame(Frame *f, float dt, const SpInput *in);     /* 1 = finished (scrolled past the end, or B) */

#endif

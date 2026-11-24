/* flashback.h - DreamSystem.BeginFlashback (tools/SPECIAL_FORMAT.md section 9): from the title's FLASHBACK item, replay
 * random recorded entity contributions (the `ec` lines of the save, g_save.ec): load that dream, put the player where they
 * stood, jump to another one every 8-16 s behind a 2 s white fade, +0.4 brightness wash on the 3D frame, for 60-360 s in
 * all, then a 3 s white fade to the title. The day never changes, nothing is saved, nothing is recorded.
 * flashbackDisabledForDays (the grey man) is kept in RAM only, like Revamped ([NonSerialized]). */
#ifndef FLASHBACK_H
#define FLASHBACK_H
#include "dream.h"
#include "player.h"
#include "raster.h"

int   fb_can(void);                    /* CanFlashback: disabled-days <= 0 and more than 100 entity contributions saved */
int   fb_active(void);                 /* InFlashback */
/* BeginFlashback: picks the first event; returns the dream to load (main fades to black over 3 s, then loads it) */
int   fb_begin(const DreamList *dl, unsigned *rng);
float fb_total_seconds(void);          /* the flashback's length (uniform 60..360 s), used as the day timer */
int   fb_env_day(void);                /* a day number for level_load: random environment / texture set per jump */
/* after level_spawn on a flashback load: the spawn override (feet position + yaw) and the event's entity switched on */
void  fb_after_load(Player *pl, Dream *d);
/* per frame while playing: 1 = time for the next jump (main: lock controls, FadeIn(white, 2 s), then load fb_next()) */
int   fb_update(float dt);
int   fb_next(const DreamList *dl, unsigned *rng);   /* picks the next event; returns its dream */
void  fb_end(void);                    /* left (title, timer, fall ...) */
void  fb_tint(Frame *f);               /* FlashbackImageEffect: rgb += 0.4 (saturating) on the rendered 3D frame */
void  fb_link_color(float rgb[3], unsigned *rng);    /* wall links in a flashback fade in a random colour (RandUtil.RandColor) */
void  fb_greyman(void);                /* the grey man touched the player: no flashbacks for 10 days */
void  fb_day_ended(void);              /* a normal day ended: one day less of that */
int   fb_disabled_days(void);

#endif

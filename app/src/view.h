/* view.h - the view distance per frame (OPTIONS > GRAPHICS > RENDER DISTANCE / AUTO LIMIT / AUTO TARGET / FOG) and the AUTO
 * controller: every 0.5 s it compares the frame's render work (projection + rasterization) with the frame budget and moves
 * the distance up when there is slack and down when the work runs over, with a dead band so it settles, eased per frame
 * (the scaled fog follows it, so it must not jump). */
#ifndef VIEW_H
#define VIEW_H

extern int g_view_now;                /* the current view distance in tiles (0 before the first dream frame): menu hint, FPS overlay */

/* the view distance (tiles) for this frame. work_us: the previous frame's project + raster time; dt: its length (s).
 * fog_end: the environment's fog end (CLASSIC fog caps the view there) */
float view_frame(float dt, unsigned work_us, float fog_end);
void  view_new_dream(void);           /* a dream was loaded: AUTO restarts from at most 60 tiles and climbs */
/* test hook (start.txt "vdist T:N ..."): from T seconds on, a fixed distance of N tiles (0 = the option, -1 = AUTO) */
void  view_test_set(int tiles);

#endif

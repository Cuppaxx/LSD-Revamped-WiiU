/* sky.h - the dream sky of LSD: Revamped: the gradient skybox (LSDR/GradientSky) and the environment effects
 * (DreamEnvironmentEffects: textured suns with sunburst rays, clouds and star fields), drawn behind the level.
 *
 * How the original draws it (load_mod.unity, Player.prefab, Prefabs/Sky, LSDR/Sun + LSDR/Background shaders):
 *  - SkyCamera (depth -6) clears to the gradient skybox over the whole screen, EffectsCamera (depth -5, layer 9 only,
 *    no clear) draws the effects, then the player camera (depth -1, no clear) draws the level on top. So every effect is
 *    behind all level geometry, and the gradient also shows below the horizon wherever there is no geometry.
 *  - The effects root sits at the player camera's position (x, z of the player, y of the camera) and has a random yaw
 *    (rolled once when the dream scene loads, i.e. once per day). Both effect cameras copy the player camera's pitch/yaw
 *    and FOV, so effect positions are simply camera-relative.
 *  - Sun: built-in Quad with LSDR/Sun: a screen-aligned billboard at local (5, SunHeight, offset), half extent
 *    SunSize/4 (quad 1 x _ScaleX = size/2), sun.png * tint * 2, alpha-tested at 0.5, queue Background+n (n = sun number).
 *    Sunburst: 4 rays (same shader and texture) at the sun, quad scaled (4 or 2) * size by size/16, random angle,
 *    re-rolled every 0.05 s, queue one below their sun.
 *  - Clouds / Stars: horizontal built-in Quads (rotated -90 about X, seen from below) at local (+-8, 2..3, +-8), scale
 *    1..4, random yaw, LSDR/Background: saturate(tex * 2) * tint, alpha blended, no fog, no depth write; vertices
 *    further than min(fog end, 20) from the camera are pushed out of the frustum (LSDR.cginc), which drops that
 *    triangle. One cloud in four drifts and/or spins (Cloud.cs). Same queue as the first sun, drawn front to back.
 * Textures come from tools/sky_assets.py (data/sky/{sun,clouds,stars}.bin). */
#ifndef SKY_H
#define SKY_H
#include "render.h"     /* Cam, Frame, Env (render.h includes sky.h after defining Cam) */

#define SKY_MAX_QUADS 48

typedef struct {
  float pos[3];           /* root-local position */
  float yaw, scale;       /* own yaw (radians, Unity Rotate about world up), uniform scale (quad side) */
  int tex;                /* 1 clouds, 2 stars */
  float tint[4];
  int moves; float t, vel[3], rot;   /* Cloud.cs: one in four can drift (units/s, root-local) and spin (radians/s) */
} SkyQuad;

typedef struct {
  float pos[3]; float scale; float tint[3];
  int burst; float burst_scale, burst_tint[3]; float ray_len[4], ray_ang[4];   /* ray_len = _ScaleX of the ray quad */
} SkySun;

typedef struct {
  int on;                 /* effects were created (HasEnvironmentEffects and the dice) */
  float root_yaw;         /* EnvironmentEffects root rotation (radians) */
  int nsun; SkySun sun[2];
  int nquad; SkyQuad quad[SKY_MAX_QUADS];
  float ray_t; unsigned rng;
} SkyFx;

/* gradient sky (LSDRSky.shader) for rows y0..y1 inclusive; also resets the depth of those rows */
void env_sky_rows(Frame *f, const Cam *cam, const Env *e, int y0, int y1);
void env_draw_sky(Frame *f, const Cam *cam, const Env *e);
/* DreamEnvironment.Apply: roll the effects of environment e. enabled = Dream.HasEnvironmentEffects, group = the
 * environment group from dream_env (-1 = plain list; suns only for day (0) / night (2) skies, stars only at night).
 * The root's random yaw is rolled again whenever `day` changes (the original rolls it per scene load = per day). */
void skyfx_build(SkyFx *fx, const Env *e, int enabled, int group, int day, unsigned *rng);
/* once per frame on the main thread, before raster_render: sunburst flicker and cloud drift (frame-time based) */
void skyfx_update(SkyFx *fx, float dt);
/* draw the effects into rows y0..y1 (after env_sky_rows of the same rows; from any raster band thread) */
void skyfx_band(Frame *f, const Cam *cam, const SkyFx *fx, int y0, int y1);
/* short text of what was rolled, for the debug overlay */
void skyfx_describe(const SkyFx *fx, char *buf, int n);

/* test knob (not used by the game): bits force the dice: 1 sun, 2 sunburst, 4 second sun, 8 clouds, 16 stars,
 * 32 ignore the environment group gating, 64 every cloud moves */
extern int g_skyfx_force;

#endif

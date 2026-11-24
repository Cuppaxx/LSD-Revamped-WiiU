/* player.h - the walking controller modelled on Revamped's PlayerMovement, PlayerRotation and PlayerCameraRotation, with
 * both control schemes: FPS ("Revamped": stick move/strafe, right stick look) and TANK ("Classic": discrete 0.2 s steps
 * forwards/backwards, turning at 120 deg/s, strafing on buttons, look up/down buttons that spring back, look behind) */
#ifndef PLAYER_H
#define PLAYER_H
#include "collide.h"
#include "render.h"

#define WALK_SPEED 0.625f      /* dream WalkSpeed of Bright Moon Cottage; the default PlayerMovement.WalkMoveSpeed is 1.25 */
#define RUN_SPEED  3.75f
#define LINK_DELAY 1.7f        /* seconds of pushing into a wall before a dream link (M3) */
#define EYE_HEIGHT 0.55f       /* controller height 0.4 + 0.15 */
#define STEP_TIME  0.2f        /* Player.prefab StepTimeSeconds: footsteps, head bob, tank steps */
#define BOB_AMOUNT 0.05f
#define TANK_TURN_DEG   120.0f /* PlayerRotation.RotationSpeed */
#define TANK_LOOK_DEG    60.0f /* PlayerCameraRotation.RotationSpeed (look up/down, and back to level) */
#define LOOK_LIMIT_DEG   70.0f /* MaxY / MinY */
#define LOOK_BEHIND_DEG 900.0f /* LOOK_BEHIND_DEGREES_PER_SECOND */
#define PAD_LOOK_DEG    98.955f /* FPS look at full stick: GAMEPAD_LOOK_SPEED 300 * DEGREES_PER_DOT 0.02199 * sensitivity 15 */

typedef struct {
  float x, y, z;               /* feet */
  float yaw, pitch;            /* body yaw (radians); FPS camera pitch (radians, + = up) */
  float vy; int grounded;
  int into_wall; float wall_t; /* LSD stops dead on touching a wall (no sliding) and links after LINK_DELAY */
  int running; float step_t, bob;
  int stepping, half_step;     /* _currentlyStepping, _playedHalfStep */
  float step_in[2], step_speed;/* TANK: the input and speed a step was taken with */
  float look_behind;           /* degrees 0..180 the camera is turned round (LookBehind) */
  float tank_pitch;            /* TANK look up/down, degrees (+ = up) */
  int footsteps;               /* footsteps to sound (the game loop plays and clears them) */
  int step_count;
} Player;

typedef struct {
  float mx, mz;                /* FPS: strafe, forward -1..1 / TANK: strafe and forward (signs are used) */
  float lx, ly;                /* FPS look stick */
  float turn;                  /* TANK: turn input (stick x), only its sign counts */
  int run;                     /* the Run action (held, or the toggle) */
  int fwd_raw;                 /* the raw move input points forwards (canStartSprinting) */
  int look_up, look_down, look_behind;
  int tank;                    /* control scheme TANK */
  int external;                /* scripted movement (UseExternalInput / StartMovingForward) */
} PlayerInput;

int  player_spawn(Player *p, const CWorld *w, int tilesw, int tilesh);          /* 1 = found a floor */
void player_update(Player *p, const CWorld *w, const PlayerInput *in, float dt, float walk_speed);
void player_camera(const Player *p, Cam *cam);
int  player_looking_behind(const Player *p);

#endif

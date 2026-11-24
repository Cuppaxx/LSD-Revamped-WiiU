#include "player.h"
#include "save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* how many of 16 probes around (x,z) (radius 1 and 2 tiles) have floor at a similar height: a spot with room to walk */
static int room_score(const CWorld *w, float x, float z, float g) {
  int s = 0;
  for (int r = 1; r <= 2; r++) for (int k = 0; k < 8; k++) {
    float a = k * 0.7853982f, gg;
    if (cw_ground(w, x + cosf(a) * r, z + sinf(a) * r, g + 0.6f, &gg) && gg > g - 0.6f) s++;
  }
  return s;
}

int player_spawn(Player *p, const CWorld *w, int tw, int th) {
  memset(p, 0, sizeof *p);
  /* look for open floor spiralling out from the middle of the chunk; fall back to the roomiest spot seen */
  float cx = tw * 0.5f - 0.5f, cz = th * 0.5f - 0.5f;
  int bestscore = -1; float bx = cx, bz = cz, by = 5;
  for (int r = 0; r <= (tw > th ? tw : th); r++) {
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      if (r > 0 && abs(dx) != r && abs(dz) != r) continue;
      float x = cx + dx, z = cz + dz, g;
      if (!cw_ground(w, x, z, 1e6f, &g)) continue;
      if (cw_blocked(w, x, z, g + PLAYER_STEP, g + PLAYER_HEIGHT, PLAYER_RADIUS * 2)) continue;
      int s = room_score(w, x, z, g);
      if (s > bestscore) { bestscore = s; bx = x; bz = z; by = g; }
      if (s == 16) goto found;
    }
  }
found:
  p->x = bx; p->y = by; p->z = bz; p->grounded = bestscore >= 0;
  return bestscore >= 0;
}

/* PlayerMovement.moveController: move by the input in the body frame; LSD stops dead at walls (no sliding) */
static void move_body(Player *p, const CWorld *w, float in_x, float in_z, float sp, float dt) {
  float sy = sinf(p->yaw), cy = cosf(p->yaw);
  float vx = (sy * in_z + cy * in_x) * sp, vz = (cy * in_z - sy * in_x) * sp;
  if (p->into_wall) {                     /* turned or steered away from the wall while still holding the stick: free again (LSD itself needed a release) */
    float px = p->x + vx * dt, pz = p->z + vz * dt;
    if (!cw_blocked(w, px, pz, p->y + PLAYER_STEP, p->y + PLAYER_HEIGHT, PLAYER_RADIUS)) { p->into_wall = 0; p->wall_t = 0; }
  }
  if (p->into_wall) { p->wall_t += dt; return; }   /* stuck: count towards the wall link */
  float dist = sqrtf(vx * vx + vz * vz) * dt;
  int steps = (int)ceilf(dist / 0.03f); if (steps < 1) steps = 1;
  float sx = vx * dt / steps, sz = vz * dt / steps;
  for (int i = 0; i < steps; i++) {
    float nx = p->x + sx, nz = p->z + sz;
    if (cw_blocked(w, nx, nz, p->y + PLAYER_STEP, p->y + PLAYER_HEIGHT, PLAYER_RADIUS)) { p->into_wall = 1; p->wall_t = 0; break; }
    p->x = nx; p->z = nz;
    float g;                             /* follow the floor up (within step height) or down (small drops) */
    if (cw_ground(w, p->x, p->z, p->y + PLAYER_STEP, &g) && p->y - g < 0.12f) { p->y = g; p->vy = 0; p->grounded = 1; }
    else p->grounded = 0;
  }
}

static void head_bob(Player *p, float u) {   /* the triangle wave of the original (HeadBobAmount 0.05) */
  p->bob = g_opt.head_bob ? 2.0f * (0.5f - fabsf(fmodf(u, 1.0f) - 0.5f)) * BOB_AMOUNT : 0.0f;
}

static float sgn(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }
static float move_towards(float a, float b, float d) { return fabsf(b - a) <= d ? b : a + (b > a ? d : -d); }

void player_update(Player *p, const CWorld *w, const PlayerInput *in, float dt, float walk_speed) {
  float mx = in->mx, mz = in->mz;
  if (!in->tank) {
    /* FPS look (PlayerCameraRotation.handleFpsCameraRotation): sticks are a rate, scaled by their deflection (curved) */
    float lx = in->lx, ly = g_opt.invert_y ? -in->ly : in->ly, m = sqrtf(lx * lx + ly * ly);
    float k = m * PAD_LOOK_DEG * (g_opt.look_sens / 5.0f) * 0.0174532925f * dt;    /* options: look speed 5 = Revamped's 15 */
    p->yaw += lx * k; p->pitch -= ly * k;
    if (p->pitch > LOOK_LIMIT_DEG * 0.0174532925f) p->pitch = LOOK_LIMIT_DEG * 0.0174532925f;
    if (p->pitch < -LOOK_LIMIT_DEG * 0.0174532925f) p->pitch = -LOOK_LIMIT_DEG * 0.0174532925f;
  } else {
    /* PlayerRotation: any turn input turns at the full rate */
    p->yaw += sgn(in->turn) * TANK_TURN_DEG * 0.0174532925f * dt;
    /* handleTankCameraRotation: look up/down while held, spring back to level otherwise */
    float r = TANK_LOOK_DEG * dt;
    if (in->look_up) p->tank_pitch += r;
    if (in->look_down) p->tank_pitch -= r;
    if (p->tank_pitch > LOOK_LIMIT_DEG) p->tank_pitch = LOOK_LIMIT_DEG;
    if (p->tank_pitch < -LOOK_LIMIT_DEG) p->tank_pitch = -LOOK_LIMIT_DEG;
    if (!in->look_up && !in->look_down) {
      if (p->tank_pitch < 0) p->tank_pitch += r; else if (p->tank_pitch > 0) p->tank_pitch -= r;
      if (fabsf(p->tank_pitch) < 2) p->tank_pitch = 0;
    }
    /* getInputDirection, classic: no partial movement, no strafing while walking */
    mx = sgn(mx); mz = sgn(mz);
    if (!in->external && mz != 0) mx = 0;
  }
  /* LookBehind: the camera swings round at 900 deg/s while held; forwards and backwards swap once it is round */
  p->look_behind = move_towards(p->look_behind, in->look_behind && !in->external ? 180.0f : 0.0f, LOOK_BEHIND_DEG * dt);
  if (!in->external && player_looking_behind(p)) { mz = -mz; if (!in->tank) mx = -mx; }

  int moving = mx != 0 || mz != 0;
  if (!moving && !p->stepping) { p->bob = 0; p->into_wall = 0; p->wall_t = 0; }
  if (in->run && in->fwd_raw && !in->external) p->running = 1;     /* canStartSprinting */

  float speed = p->running ? RUN_SPEED : walk_speed;
  if (!in->tank) {
    if (moving) {
      if (p->stepping) p->step_t += dt;                            /* TimeSince the step started */
      if (!p->stepping || p->step_t > STEP_TIME) { p->stepping = 1; p->step_t = 0; p->half_step = 0; p->footsteps++; p->step_count++; }
      if (p->running && !p->half_step && p->step_t > STEP_TIME / 2) { p->half_step = 1; p->footsteps++; }   /* running: one more half way */
      head_bob(p, p->step_t / STEP_TIME);
      move_body(p, w, mx, mz, speed, dt);
    } else p->stepping = 0;
  } else {
    /* takeStep: a 0.2 s step with the input and speed it started with; a footstep at its start, one more half way when running */
    if (!p->stepping && moving) {
      p->stepping = 1; p->step_t = 0; p->half_step = 0; p->step_in[0] = mx; p->step_in[1] = mz; p->step_speed = speed;
      p->footsteps++; p->step_count++;
    }
    if (p->stepping) {
      p->step_t += dt;
      if (p->step_t >= STEP_TIME) { move_body(p, w, p->step_in[0], p->step_in[1], p->step_speed, dt); p->stepping = 0; }
      else {
        float prog = p->step_t / STEP_TIME;
        if (p->running && !p->half_step && prog > 0.5f) { p->half_step = 1; p->footsteps++; }
        move_body(p, w, p->step_in[0], p->step_in[1], p->step_speed, dt);
        head_bob(p, prog);
      }
    }
  }
  if (!in->fwd_raw || in->external) p->running = 0;                 /* no longer moving forwards: no longer sprinting */

  /* gravity */
  float g;
  int have = cw_ground(w, p->x, p->z, p->y + PLAYER_STEP, &g);
  if (have && p->y - g < 0.12f && p->vy <= 0) { p->y = g; p->vy = 0; p->grounded = 1; }
  else {
    p->grounded = 0; p->vy -= 9.81f * dt;
    float ny = p->y + p->vy * dt;
    if (have && ny <= g) { p->y = g; p->vy = 0; p->grounded = 1; } else p->y = ny;
  }
}

int player_looking_behind(const Player *p) { return p->look_behind >= 180.0f; }

void player_camera(const Player *p, Cam *cam) {
  cam->x = p->x; cam->z = p->z; cam->y = p->y + EYE_HEIGHT + p->bob;
  cam->yaw = p->yaw + p->look_behind * 0.0174532925f;
  cam->pitch = g_opt.scheme ? p->tank_pitch * 0.0174532925f : p->pitch;
}

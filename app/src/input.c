/* input.c - see input.h */
#include "input.h"
#include "save.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void lsd_log(const char *fmt, ...);

InputState g_in;

enum { DEV_GAMEPAD, DEV_PRO, DEV_CLASSIC, DEV_NUNCHUK, DEV_REMOTE, DEV_OTHER };
static const char *dev_names[] = { "GAMEPAD", "PRO CONTROLLER", "CLASSIC", "WIIMOTE+NUNCHUK", "WIIMOTE", "JOYSTICK" };
typedef struct { SDL_Joystick *js; SDL_JoystickID id; int type; int down[BN]; float ax[4]; int hat; } Dev;
#define MAX_DEV 8
static Dev devs[MAX_DEV]; static int ndev;

/* a Wii Remote alone, held sideways (SDL's built-in "Wii Remote" mapping: a:b7 b:b6 x:b1 y:b0 dpup:b14 dpleft:b13
 * dpright:b15 dpdown:b12): 2 = A, 1 = B, the A button = X, the B trigger = ZL, the d-pad turned a quarter */
static const signed char remote_map[16] = { BX, BZL, -1, -1, -1, -1, BB, BA, -1, -1, BPLUS, BMINUS, BDOWN, BLEFT, BUP, BRIGHT };

static const char *bnames[BN] = { "A", "B", "X", "Y", "L STICK", "R STICK", "L", "R", "ZL", "ZR", "+", "-", "LEFT", "UP", "RIGHT", "DOWN" };
static const char *anames[ACT_N] = { "RUN", "INTERACT", "LOOK BEHIND", "LOOK UP", "LOOK DOWN", "STRAFE LEFT", "STRAFE RIGHT", "PAUSE" };
const char *input_button_name(int b) { return b >= 0 && b < BN ? bnames[b] : "-"; }
const char *input_action_name(int a) { return a >= 0 && a < ACT_N ? anames[a] : "?"; }
void input_default_bindings(int *bind) {
  static const int def[ACT_N] = { BB, BA, BZL, BX, BY, BL, BR, BPLUS };
  memcpy(bind, def, sizeof def);
}

static int guid_type(SDL_Joystick *js, int index) {
  SDL_JoystickGUID g = SDL_JoystickGetGUID(js);
  char s[13]; memcpy(s, g.data + 4, 12); s[12] = 0;
  if (!strncmp(s, "Wii U Gamep", 11)) return DEV_GAMEPAD;
  if (!strncmp(s, "Wii U Pro C", 11)) return DEV_PRO;
  if (!strncmp(s, "Wii Classic", 11)) return DEV_CLASSIC;
  if (!strncmp(s, "Wii Nunchuk", 11)) return DEV_NUNCHUK;
  if (!strncmp(s, "Wii Remote", 10)) return DEV_REMOTE;
  const char *n = SDL_JoystickName(js);
  if (n && strstr(n, "Gamepad")) return DEV_GAMEPAD;
  return index == 0 ? DEV_GAMEPAD : DEV_OTHER;
}

static Dev *dev_of(SDL_JoystickID id) { for (int i = 0; i < ndev; i++) if (devs[i].id == id) return &devs[i]; return NULL; }

static void open_dev(int index) {
  SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
  if (dev_of(id) || ndev >= MAX_DEV) return;
  SDL_Joystick *js = SDL_JoystickOpen(index); if (!js) return;
  Dev *d = &devs[ndev++]; memset(d, 0, sizeof *d); d->js = js; d->id = SDL_JoystickInstanceID(js); d->type = guid_type(js, index);
  SDL_JoystickGUID g = SDL_JoystickGetGUID(js); char gs[40]; SDL_JoystickGetGUIDString(g, gs, sizeof gs);
  lsd_log("input: joystick %d '%s' guid %s -> %s (%d buttons, %d axes, %d hats)", index, SDL_JoystickName(js) ? SDL_JoystickName(js) : "?", gs,
          dev_names[d->type], SDL_JoystickNumButtons(js), SDL_JoystickNumAxes(js), SDL_JoystickNumHats(js));
}

void input_init(void) {
  SDL_JoystickEventState(SDL_ENABLE);
  for (int i = 0; i < SDL_NumJoysticks(); i++) open_dev(i);
}

static int map_button(Dev *d, int b) {
  if (d->type == DEV_REMOTE) return b >= 0 && b < 16 ? remote_map[b] : -1;
  return b >= 0 && b < BN ? b : -1;
}

int input_event(const SDL_Event *e) {
  switch (e->type) {
    case SDL_JOYDEVICEADDED: open_dev(e->jdevice.which); return -1;
    case SDL_JOYDEVICEREMOVED: {
      Dev *d = dev_of(e->jdevice.which); if (!d) return -1;
      lsd_log("input: %s disconnected", dev_names[d->type]);
      SDL_JoystickClose(d->js); *d = devs[--ndev]; return -1; }
    case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP: {
      Dev *d = dev_of(e->jbutton.which); if (!d) return -1;
      int b = map_button(d, e->jbutton.button); if (b < 0) return -1;
      int dn = e->type == SDL_JOYBUTTONDOWN;
      d->down[b] = dn;
      if (dn) { g_in.pressed[b] = 1; g_in.touched = 1; return b; }
      return -1; }
    case SDL_JOYHATMOTION: {
      Dev *d = dev_of(e->jhat.which); if (!d) return -1;
      int h = e->jhat.value, old = d->hat; d->hat = h;
      static const int hb[4] = { SDL_HAT_LEFT, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN };
      for (int k = 0; k < 4; k++) if ((h & hb[k]) && !(old & hb[k])) { g_in.pressed[BLEFT + k] = 1; g_in.touched = 1; }
      return -1; }
    case SDL_JOYAXISMOTION: {
      Dev *d = dev_of(e->jaxis.which); if (!d) return -1;
      if (e->jaxis.axis < 4) d->ax[e->jaxis.axis] = e->jaxis.value / 32767.0f;
      return -1; }
    case SDL_FINGERDOWN:                                            /* GamePad touch screen: top right corner = pause */
      g_in.touched = 1;
      if (e->tfinger.x > 0.85f && e->tfinger.y < 0.18f) g_in.corner_tap = 1;
      g_in.tap = 1; g_in.tap_x = e->tfinger.x; g_in.tap_y = e->tfinger.y;   /* menus: tap an item (menu_tap) */
      lsd_log("input: touch %.2f,%.2f%s", e->tfinger.x, e->tfinger.y, g_in.corner_tap ? " (pause corner)" : "");
      return -1;
  }
  return -1;
}

/* test hook schedule */
typedef struct { int btn, axis; float sign, t0, t1; } Hold;
static Hold holds[64]; static int nholds;
void input_test_hold(const char *name, float t0, float secs) {
  if (nholds >= 64) return;
  Hold h = { -1, -1, 0, t0, t0 + secs };
  if ((name[0] == 'L' || name[0] == 'R') && (name[1] == 'X' || name[1] == 'Y') && (name[2] == '+' || name[2] == '-')) {
    h.axis = (name[0] == 'R') * 2 + (name[1] == 'Y'); h.sign = name[2] == '+' ? 1.0f : -1.0f;
  } else {
    for (int b = 0; b < BN; b++) if (!strcmp(name, bnames[b])) h.btn = b;
    if (!strcmp(name, "LS")) h.btn = BLS;
    if (!strcmp(name, "RS")) h.btn = BRS;
    if (!strcmp(name, "PLUS")) h.btn = BPLUS;
    if (!strcmp(name, "MINUS")) h.btn = BMINUS;
    if (h.btn < 0) { lsd_log("input: test pad name '%s' unknown", name); return; }
  }
  holds[nholds++] = h;
}

static float dz_stick(float *x, float *y, float dz) {
  float m = sqrtf(*x * *x + *y * *y);
  if (m <= dz || m < 1e-6f) { *x = *y = 0; return 0; }
  float k = (fminf(m, 1.0f) - dz) / (1.0f - dz) / m; *x *= k; *y *= k;
  return m;
}

void input_frame(float now) {
  float dz = g_opt.deadzone * 0.05f;
  int down[BN] = {0}; float best[2] = {0, 0}; float st[4] = {0, 0, 0, 0};
  for (int i = 0; i < ndev; i++) {
    Dev *d = &devs[i];
    for (int b = 0; b < BN; b++) down[b] |= d->down[b];
    if (d->hat & SDL_HAT_LEFT) down[BLEFT] = 1;
    if (d->hat & SDL_HAT_UP) down[BUP] = 1;
    if (d->hat & SDL_HAT_RIGHT) down[BRIGHT] = 1;
    if (d->hat & SDL_HAT_DOWN) down[BDOWN] = 1;
    for (int s = 0; s < 2; s++) {
      if (d->type == DEV_REMOTE) continue;                             /* no sticks */
      float x = d->ax[s * 2], y = d->ax[s * 2 + 1], m = dz_stick(&x, &y, dz);
      if (m > best[s]) { best[s] = m; st[s * 2] = x; st[s * 2 + 1] = y; }
    }
  }
  for (int k = 0; k < nholds; k++) {                                   /* test hook: scripted holds */
    Hold *h = &holds[k];
    if (now < h->t0 || now >= h->t1) continue;
    if (h->btn >= 0) { if (!down[h->btn] && !g_in.down[h->btn]) g_in.pressed[h->btn] = 1; down[h->btn] = 1; }
    else st[h->axis] = h->sign;
    g_in.touched = 1;
  }
  memcpy(g_in.down, down, sizeof down);
  g_in.lx = st[0]; g_in.ly = st[1]; g_in.rx = st[2]; g_in.ry = st[3];
  if (best[0] > 0 || best[1] > 0) g_in.touched = 1;
}

void input_end_frame(void) { memset(g_in.pressed, 0, sizeof g_in.pressed); g_in.touched = 0; g_in.corner_tap = 0; g_in.tap = 0; }

int input_act_down(int act) { int b = act >= 0 && act < ACT_N ? g_opt.bind[act] : -1; return b >= 0 && b < BN && g_in.down[b]; }
int input_act_pressed(int act) { int b = act >= 0 && act < ACT_N ? g_opt.bind[act] : -1; return b >= 0 && b < BN && g_in.pressed[b]; }

int input_devices(char *out, int cap) {
  int n = 0; out[0] = 0;
  for (int i = 0; i < ndev; i++) { n += snprintf(out + n, n < cap ? (size_t)(cap - n) : 0, "%s%s", i ? ", " : "", dev_names[devs[i].type]); if (n >= cap) break; }
  return ndev;
}

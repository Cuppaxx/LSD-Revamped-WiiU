/* input.h - every connected controller at once: the Wii U GamePad (SDL joystick 0), Wii U Pro Controllers, Classic
 * Controllers, Wii Remotes (+ Nunchuk). SDL2-wiiu numbers the buttons of the GamePad, Pro, Classic and Nunchuk
 * joysticks the same way (A B X Y LS RS L R ZL ZR + - Left Up Right Down = 0..15, sticks axes 0..3; read from the
 * controller mappings built into libSDL2.a); a bare Wii Remote is held sideways and gets its own table. The device type
 * comes from the joystick GUID ("Wii U Gamep", "Wii U Pro C", "Wii Classic", "Wii Nunchuk", "Wii Remote").
 * Buttons are merged (any device), sticks come from the device pushing furthest, with the deadzone option applied. */
#ifndef INPUT_H
#define INPUT_H
#include <SDL2/SDL.h>

/* logical buttons (= the GamePad's SDL numbering) */
enum { BA, BB, BX, BY, BLS, BRS, BL, BR, BZL, BZR, BPLUS, BMINUS, BLEFT, BUP, BRIGHT, BDOWN, BN };
/* rebindable game actions (Revamped's InputActions Game map) */
enum { ACT_RUN, ACT_INTERACT, ACT_LOOKBEHIND, ACT_LOOKUP, ACT_LOOKDOWN, ACT_STRAFEL, ACT_STRAFER, ACT_PAUSE, ACT_N };

typedef struct {
  int down[BN], pressed[BN];   /* held now / went down this frame (any device) */
  float lx, ly, rx, ry;        /* sticks after the deadzone, -1..1 (y down = +) */
  int touched;                 /* any input this frame (buttons, sticks past the deadzone, touch) */
  int corner_tap;              /* GamePad touch screen: a tap in the top right corner (pause) */
  int tap; float tap_x, tap_y; /* GamePad touch screen: a tap this frame at (0..1, 0..1) of the screen */
} InputState;
extern InputState g_in;

void input_init(void);                     /* opens the joysticks present now (more are opened when they connect) */
/* feed an SDL event; returns the logical button for a button-down (else -1) */
int  input_event(const SDL_Event *e);
void input_frame(float now_seconds);       /* call once per frame after the events: test schedule, sticks, deadzone; clears edges of the previous frame first */
void input_end_frame(void);                /* clears the pressed edges */
int  input_act_down(int act);              /* the bound button is held */
int  input_act_pressed(int act);
const char *input_button_name(int b);
const char *input_action_name(int act);
void input_default_bindings(int *bind);    /* ACT_N entries */
int  input_devices(char *out, int cap);    /* "GAMEPAD, PRO 2" for the log/menu; returns the count */
/* test hook (start.txt "pad T:NAME:SECS ..."): hold a logical button or a full stick deflection (LX+ LX- LY+ LY- RX+ ...) */
void input_test_hold(const char *name, float t0, float secs);

#endif

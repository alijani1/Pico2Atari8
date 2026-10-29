/* a8_input.cpp - the Quad Ultimate CX's controls (GameAdapterRGBPico
 * data/quad.json). Every control is a direct GPIO, active LOW with the
 * internal pull-up; the Joy-Con stick is two ADC channels.
 *
 *   Ports 1-4 (DB9)   Atari joysticks 1-4 (the 400/800 had four ports;
 *                     XL/XE machines read only the first two)
 *   Joy-Con stick     also joystick 1; its click = fire 1
 *   Red / Blue        fire 1 / RETURN
 *   Green / Yellow    OPTION / SPACE
 *   START / SELECT    START / SELECT (GPIO 20 / 19, as in the arcade)
 *   MENU              hold 1 s in a game: back to the game list
 *   Side buttons      game list: up / down; in a game, left = FPS overlay
 *                     on / off
 */
#include <Arduino.h>
#include "a8_input.h"
#include "a8_platform.h"
#include "a8_display.h"

extern "C" {
#include "akey.h"
}

static const int8_t PORT_PINS[4][5] = {     /* up down left right fire */
    { 28, 29, 30, 31, 12 },
    { 24, 25, 26, 27, 10 },
    { 32, 33, 34, 35, 11 },
    { 36, 37, 38, 39, 13 },
};
static const int PIN_RED = 14, PIN_GREEN = 15, PIN_BLUE = 16, PIN_YELLOW = 17;
static const int PIN_SELECT = 19, PIN_START = 20, PIN_MENU = 18, PIN_CLICK = 21;
static const int PIN_SIDE_UP = 3, PIN_SIDE_DOWN = 2;
static const int PIN_JOY_X = 44, PIN_JOY_Y = 45;
static const int JOY_THRESHOLD = 512;       /* 25 % of the stick's travel */

static int joy_cx = 2048, joy_cy = 2048;

static inline bool low(int p) { return digitalRead(p) == LOW; }

void a8_input_init(void) {
    for (auto &port : PORT_PINS)
        for (int8_t p : port) pinMode(p, INPUT_PULLUP);
    const int buttons[] = { PIN_RED, PIN_GREEN, PIN_BLUE, PIN_YELLOW, PIN_SELECT,
                            PIN_START, PIN_MENU, PIN_CLICK, PIN_SIDE_UP, PIN_SIDE_DOWN };
    for (int p : buttons) pinMode(p, INPUT_PULLUP);
    /* Joy-Con centre: it rests at power-up */
    analogReadResolution(12);
    long sx = 0, sy = 0;
    for (int i = 0; i < 8; i++) { sx += analogRead(PIN_JOY_X); sy += analogRead(PIN_JOY_Y); delay(2); }
    joy_cx = (int)(sx / 8);
    joy_cy = (int)(sy / 8);
}

static uint8_t joycon_dirs(void) {
    int jx = analogRead(PIN_JOY_X) - joy_cx;
    int jy = analogRead(PIN_JOY_Y) - joy_cy;
    uint8_t d = 0;
    if (jx < -JOY_THRESHOLD) d |= A8_LEFT;
    if (jx >  JOY_THRESHOLD) d |= A8_RIGHT;
    if (jy < -JOY_THRESHOLD) d |= A8_UP;
    if (jy >  JOY_THRESHOLD) d |= A8_DOWN;
    return d;
}

static uint8_t port_dirs(int n) {
    uint8_t d = 0;
    if (low(PORT_PINS[n][0])) d |= A8_UP;
    if (low(PORT_PINS[n][1])) d |= A8_DOWN;
    if (low(PORT_PINS[n][2])) d |= A8_LEFT;
    if (low(PORT_PINS[n][3])) d |= A8_RIGHT;
    return d;
}

/* ---- in a game ------------------------------------------------------- */

static uint32_t menu_since = 0;
static bool menu_seen_up = false;

bool a8_input_frame(void) {
    a8_input_t in;
    memset(&in, 0, sizeof(in));
    for (int n = 0; n < 4; n++) {
        in.stick[n] = port_dirs(n);
        in.trig[n] = low(PORT_PINS[n][4]);
    }
    in.stick[0] |= joycon_dirs();
    if (low(PIN_RED) || low(PIN_CLICK)) in.trig[0] = 1;
    in.start  = low(PIN_START);
    in.select = low(PIN_SELECT);
    in.option = low(PIN_GREEN);
    in.key = -1;
    if (low(PIN_BLUE))        in.key = AKEY_RETURN;
    else if (low(PIN_YELLOW)) in.key = AKEY_SPACE;
    a8_input = in;

    /* Left side button: FPS overlay on / off (on press) */
    static bool side_was = true;           /* ignore one still held from the menu */
    bool side = low(PIN_SIDE_UP);
    if (side && !side_was) a8_display_toggle_fps();
    side_was = side;

    /* MENU held 1 s: leave the game. A press already down when the game
     * started (from the menu) counts only after it has been released. */
    bool m = low(PIN_MENU);
    if (!m) { menu_seen_up = true; menu_since = 0; return false; }
    if (!menu_seen_up) return false;
    uint32_t now = millis();
    if (!menu_since) menu_since = now | 1;
    return now - menu_since >= 1000;
}

/* ---- game list ------------------------------------------------------- */

uint8_t a8_input_menu(void) {
    uint8_t b = 0;
    uint8_t d = port_dirs(0) | joycon_dirs();
    if ((d & A8_UP)   || low(PIN_SIDE_UP))   b |= A8M_UP;
    if ((d & A8_DOWN) || low(PIN_SIDE_DOWN)) b |= A8M_DOWN;
    if (d & A8_LEFT)  b |= A8M_LEFT;
    if (d & A8_RIGHT) b |= A8M_RIGHT;
    if (low(PORT_PINS[0][4]) || low(PIN_RED) || low(PIN_CLICK) || low(PIN_START)) b |= A8M_OK;
    if (low(PIN_MENU)) b |= A8M_BACK;
    return b;
}

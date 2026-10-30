/* a8_input.cpp - the Quad Ultimate CX's controls (GameAdapterRGBPico
 * data/quad.json). Every control is a direct GPIO, active LOW with the
 * internal pull-up; the Joy-Con stick is two ADC channels.
 *
 *   Ports 1-4 (DB9)   Atari joysticks 1-4 (the 400/800 had four ports;
 *                     XL/XE machines read only the first two)
 *   Joy-Con stick     also joystick 1; its click = fire 1
 *   Red               fire 1
 *   Blue              on-screen keyboard: opens / closes (fire types the
 *                     key; Settings > Keyboard)
 *   Green / Yellow    OPTION / SPACE
 *   START / SELECT    START / SELECT (GPIO 20 / 19, as in the arcade)
 *   MENU              hold 1 s in a game: back to the game list
 *   Side buttons      game list: up / down; in a game, right = RETURN
 *
 * Duo 5200 Ultimate (PICO_5200, GameAdapterRGBPico data/duo5200.json): the
 * same device buttons and Joy-Con, but the ports are two Atari 5200
 * controllers instead of DB9 joysticks, and there are no side buttons:
 *
 *   5200 controllers  joysticks 1-2: the analog stick as directions (RC
 *                     charge time, as Pico2Arcade reads it); Fire A or
 *                     Fire B = fire
 *   (keypads)         not read yet - the on-screen keyboard (Blue) types
 *
 * A 5200 game (either device; a8_is_5200) has its own controls:
 *
 *   Joy-Con stick     controller 1's analog stick (POKEY pots 0/1)
 *   Red / click       bottom fire;  Green  top fire
 *   START / SELECT    START / PAUSE;  Yellow  *
 *   Blue              the keypad on screen: 0-9 * # START PAUSE RESET
 *   5200 controllers  (Duo) directions and Fire A as before, Fire B = top
 *                     fire; the analog stick and keypad come in phase 2
 */
#include <Arduino.h>
#include "a8_input.h"
#include "a8_platform.h"
#include "a8_display.h"
#include "a8_osk.h"
#include "a8_settings.h"

extern "C" {
#include "akey.h"
}

#ifdef PICO_5200
static const int NUM_PORTS = 2;
static const int8_t RC_PINS[2][2]   = { { 40, 41 }, { 42, 43 } };   /* stick x, y */
static const int8_t FIRE_PINS[2][2] = { { 10, 11 }, { 12, 13 } };   /* Fire A, Fire B */
static const int PIN_SIDE_UP = -1, PIN_SIDE_DOWN = -1;              /* none */
#else
static const int NUM_PORTS = 4;
static const int8_t PORT_PINS[4][5] = {     /* up down left right fire */
    { 28, 29, 30, 31, 12 },
    { 24, 25, 26, 27, 10 },
    { 32, 33, 34, 35, 11 },
    { 36, 37, 38, 39, 13 },
};
static const int PIN_SIDE_UP = 3, PIN_SIDE_DOWN = 2;
#endif
static const int PIN_RED = 14, PIN_GREEN = 15, PIN_BLUE = 16, PIN_YELLOW = 17;
static const int PIN_SELECT = 19, PIN_START = 20, PIN_MENU = 18, PIN_CLICK = 21;
static const int PIN_JOY_X = 44, PIN_JOY_Y = 45;

/* ---- Joy-Con ---------------------------------------------------------
 * The centre is assumed at the first reading (averaged at start-up); as the
 * stick moves, the lowest and highest readings are kept, and once it has
 * gone well past the centre BOTH ways the centre becomes their midpoint
 * (one side alone would skew it). A direction counts outside a dead zone
 * of 30 % of the half-range around the centre; until both sides are known,
 * a nominal half-range stands in. */
struct JoyAxis { int pin; int first, lo, hi; };
static const int NOMINAL_HALF = 1000;       /* of 2048: until it has moved */
static JoyAxis joy_x = { PIN_JOY_X }, joy_y = { PIN_JOY_Y };

static void axis_init(JoyAxis &a, int v) { a.first = a.lo = a.hi = v; }

/* Centre and half-range in use (see above). */
static void axis_centre(const JoyAxis &a, int *centre, int *half) {
    bool both = a.first - a.lo >= NOMINAL_HALF * 6 / 10 && a.hi - a.first >= NOMINAL_HALF * 6 / 10;
    *centre = both ? (a.lo + a.hi) / 2 : a.first;
    *half = both ? (a.hi - a.lo) / 2 : NOMINAL_HALF;
}

static int axis_sample(JoyAxis &a) {
    int v = analogRead(a.pin);
    if (v < a.lo) a.lo = v;
    if (v > a.hi) a.hi = v;
    return v;
}

static int8_t axis_dir(const JoyAxis &a, int v) {
    int centre, half;
    axis_centre(a, &centre, &half);
    int dead = half * 30 / 100;
    if (v < centre - dead) return -1;
    if (v > centre + dead) return 1;
    return 0;
}

/* As a 5200 pot: 6 (left / up) .. 114 (centre) .. 220 (right / down), the
 * travel past a small dead zone spread over the whole range; inside the
 * dead zone A8_POT_NONE (the core then sees the centre). */
static const int POT_MIN = 6, POT_CENTRE = 114, POT_MAX = 220;
static int16_t axis_pot(const JoyAxis &a, int v) {
    int centre, half;
    axis_centre(a, &centre, &half);
    int dead = half * 8 / 100, d = v - centre;
    if (d >= -dead && d <= dead) return A8_POT_NONE;
    int range = half - dead;
    int p = d > 0 ? POT_CENTRE + (d - dead) * (POT_MAX - POT_CENTRE) / range
                  : POT_CENTRE + (d + dead) * (POT_CENTRE - POT_MIN) / range;
    if (p < POT_MIN) p = POT_MIN;
    if (p > POT_MAX) p = POT_MAX;
    return (int16_t)p;
}

static inline bool low(int p) { return p >= 0 && digitalRead(p) == LOW; }

#ifdef PICO_5200
/* ---- Atari 5200 controller sticks -------------------------------------
 * Ported from Pico2Arcade (src/pico_input.cpp rc_stick_read): each pot's
 * capacitor rests discharged (OUTPUT LOW); released to INPUT it charges
 * through the pot, and the time until the pin reads HIGH grows with the
 * resistance - longer = right (X) / down (Y), about 0-1.3 ms over the
 * stick's travel. Blocks for up to 2 ms, so it runs at most every 16 ms.
 * Centre = where the stick rests at its first reading; a direction triggers
 * at 25 % of the travel seen on that side of it (the nominal travel until
 * the stick has moved). Unplugged (both axes time out) = start over. */
static const uint32_t RC_TIMEOUT_US = 2000;     /* no controller / pot beyond range */
static const uint32_t RC_NOMINAL_US = 1334;     /* full travel, nominal */
static uint8_t rc_dirs[2];                      /* A8_UP/DOWN/LEFT/RIGHT per port */

static void rc_init(void) {
    for (auto &port : RC_PINS)
        for (int8_t p : port) {
            gpio_init(p);
            gpio_disable_pulls(p);
            gpio_put(p, 0);
            gpio_set_dir(p, GPIO_OUT);          /* park discharged */
        }
}

static void rc_read(void) {
    static uint32_t lastUs = 0;
    static uint32_t lo[2][2] = { { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 0xFFFFFFFFu, 0xFFFFFFFFu } };
    static uint32_t hi[2][2];
    static int32_t  rest[2][2] = { { -1, -1 }, { -1, -1 } };   /* charge time at rest */
    uint32_t now = time_us_32();
    if (lastUs != 0 && now - lastUs < 16000) return;
    lastUs = now;

    uint32_t t[2][2] = { { 0, 0 }, { 0, 0 } };
    uint32_t t0 = time_us_32();
    for (auto &port : RC_PINS)                  /* release all four caps together */
        for (int8_t p : port) gpio_set_dir(p, GPIO_IN);
    for (;;) {
        uint32_t el = time_us_32() - t0;
        bool all = true;
        for (int n = 0; n < 2; n++)
            for (int a = 0; a < 2; a++) {
                if (!t[n][a] && gpio_get(RC_PINS[n][a])) t[n][a] = el + 1;
                if (!t[n][a]) all = false;
            }
        if (all || el >= RC_TIMEOUT_US) break;
    }
    for (auto &port : RC_PINS)                  /* discharge again (pin LOW) */
        for (int8_t p : port) gpio_set_dir(p, GPIO_OUT);

    for (int n = 0; n < 2; n++) {
        if (!t[n][0] && !t[n][1]) {             /* no controller: forget its calibration */
            for (int a = 0; a < 2; a++) { lo[n][a] = 0xFFFFFFFFu; hi[n][a] = 0; rest[n][a] = -1; }
            rc_dirs[n] = 0;
            continue;
        }
        uint8_t dirs = 0;
        for (int a = 0; a < 2; a++) {
            if (!t[n][a]) continue;             /* timed out: no pot on this axis */
            if (rest[n][a] < 0) rest[n][a] = (int32_t)t[n][a];   /* first reading: at rest */
            if (t[n][a] < lo[n][a]) lo[n][a] = t[n][a];
            if (t[n][a] > hi[n][a]) hi[n][a] = t[n][a];
            const int32_t minSpan = RC_NOMINAL_US / 4, nomThr = RC_NOMINAL_US / 8;
            int32_t spanNeg = rest[n][a] - (int32_t)lo[n][a], spanPos = (int32_t)hi[n][a] - rest[n][a];
            int32_t thrNeg = (spanNeg >= minSpan) ? spanNeg / 4 : nomThr;
            int32_t thrPos = (spanPos >= minSpan) ? spanPos / 4 : nomThr;
            int32_t d = (int32_t)t[n][a] - rest[n][a];
            if (d >  thrPos) dirs |= (a == 0) ? A8_RIGHT : A8_DOWN;
            if (d < -thrNeg) dirs |= (a == 0) ? A8_LEFT  : A8_UP;
        }
        rc_dirs[n] = dirs;
    }
}
#endif

void a8_input_init(void) {
#ifdef PICO_5200
    for (auto &port : FIRE_PINS)
        for (int8_t p : port) pinMode(p, INPUT_PULLUP);
    rc_init();
#else
    for (auto &port : PORT_PINS)
        for (int8_t p : port) pinMode(p, INPUT_PULLUP);
#endif
    const int buttons[] = { PIN_RED, PIN_GREEN, PIN_BLUE, PIN_YELLOW, PIN_SELECT,
                            PIN_START, PIN_MENU, PIN_CLICK, PIN_SIDE_UP, PIN_SIDE_DOWN };
    for (int p : buttons) if (p >= 0) pinMode(p, INPUT_PULLUP);
    /* Joy-Con: a first guess at the centre; it keeps learning (axis_sample) */
    analogReadResolution(12);
    delay(20);                               /* let the ADC settle */
    long sx = 0, sy = 0;
    for (int i = 0; i < 32; i++) { sx += analogRead(PIN_JOY_X); sy += analogRead(PIN_JOY_Y); delay(1); }
    axis_init(joy_x, (int)(sx / 32));
    axis_init(joy_y, (int)(sy / 32));
}

/* One Joy-Con reading: its directions, and (pot_x/pot_y given) its
 * position as 5200 pots. */
static uint8_t joycon_read(int16_t *pot_x = nullptr, int16_t *pot_y = nullptr) {
    int vx = axis_sample(joy_x), vy = axis_sample(joy_y);
    if (pot_x) *pot_x = axis_pot(joy_x, vx);
    if (pot_y) *pot_y = axis_pot(joy_y, vy);
    int8_t x = axis_dir(joy_x, vx), y = axis_dir(joy_y, vy);
    uint8_t d = 0;
    if (x < 0) d |= A8_LEFT;
    if (x > 0) d |= A8_RIGHT;
    if (y < 0) d |= A8_UP;
    if (y > 0) d |= A8_DOWN;
    return d;
}

#ifdef PICO_5200
static uint8_t port_dirs(int n) { rc_read(); return rc_dirs[n]; }
static bool port_fire(int n) { return low(FIRE_PINS[n][0]) || low(FIRE_PINS[n][1]); }
static bool port_fire_a(int n) { return low(FIRE_PINS[n][0]); }
static bool port_fire_b(int n) { return low(FIRE_PINS[n][1]); }
#else
static uint8_t port_dirs(int n) {
    uint8_t d = 0;
    if (low(PORT_PINS[n][0])) d |= A8_UP;
    if (low(PORT_PINS[n][1])) d |= A8_DOWN;
    if (low(PORT_PINS[n][2])) d |= A8_LEFT;
    if (low(PORT_PINS[n][3])) d |= A8_RIGHT;
    return d;
}
static bool port_fire(int n) { return low(PORT_PINS[n][4]); }
static bool port_fire_a(int n) { return port_fire(n); }
static bool port_fire_b(int n) { (void)n; return false; }
#endif

/* ---- in a game ------------------------------------------------------- */

static uint32_t menu_since = 0;
static bool menu_seen_up = false;

static bool osk_running = false;

bool a8_input_osk_paused(void) {
    return a8_osk_is_open() && !osk_running && a8_settings.keyboard != A8_KBD_KEEP_RUNNING;
}

bool a8_input_frame(void) {
    a8_input_t in;
    memset(&in, 0, sizeof(in));
    for (int n = 0; n < 8; n++) in.pot[n] = A8_POT_NONE;
    in.key = -1;
    uint8_t nav;                          /* moves the on-screen keyboard's cursor */
    if (a8_is_5200()) {
        /* the Joy-Con is controller 1's analog stick; a controller's own
         * directions count while the Joy-Con rests (input.c) */
        for (int n = 0; n < NUM_PORTS; n++) {
            in.stick[n] = port_dirs(n);
            in.trig[n] = port_fire_a(n);
            if (port_fire_b(n)) in.shift = 1;
        }
        nav = in.stick[0] | joycon_read(&in.pot[0], &in.pot[1]);
        if (low(PIN_RED) || low(PIN_CLICK)) in.trig[0] = 1;
        if (low(PIN_GREEN)) in.shift = 1;                 /* top fire */
        if (low(PIN_YELLOW)) in.key = AKEY_5200_ASTERISK;
        if (low(PIN_SELECT)) in.key = AKEY_5200_PAUSE;
        if (low(PIN_START))  in.key = AKEY_5200_START;
    } else {
        for (int n = 0; n < NUM_PORTS; n++) {
            in.stick[n] = port_dirs(n);
            in.trig[n] = port_fire(n);
        }
        in.stick[0] |= joycon_read();
        nav = in.stick[0];
        if (low(PIN_RED) || low(PIN_CLICK)) in.trig[0] = 1;
        in.start  = low(PIN_START);
        in.select = low(PIN_SELECT);
        in.option = low(PIN_GREEN);
        if (low(PIN_YELLOW)) in.key = AKEY_SPACE;

        /* Right side button: RETURN. */
        if (low(PIN_SIDE_DOWN)) in.key = AKEY_RETURN;
    }

    /* Blue: the on-screen keyboard, open / close on each press. (It used to
     * be RETURN on a tap as well, and a short first press typed a RETURN
     * before the keyboard opened.) */
    static bool blue_was = true;          /* one still held from the menu: ignored */
    uint32_t t = millis();
    bool blue = low(PIN_BLUE);
    if (blue && !blue_was) {
        if (a8_osk_is_open()) a8_osk_close();
        else a8_osk_open();
    }
    blue_was = blue;

    /* The keyboard takes joystick 1 and its fire: the stick moves the
     * cursor (auto-repeat), fire types the key (held while fire is). */
    if (a8_osk_is_open()) {
        static uint8_t dirs_was = 0;
        static uint32_t repeat_at = 0;
        uint8_t dirs = nav;
        uint8_t edge = dirs & ~dirs_was;
        if (dirs && dirs == dirs_was && (int32_t)(t - repeat_at) >= 0) { edge = dirs; repeat_at = t + 120; }
        else if (edge) repeat_at = t + 350;
        dirs_was = dirs;
        int dx = (edge & A8_RIGHT) ? 1 : (edge & A8_LEFT) ? -1 : 0;
        int dy = (edge & A8_DOWN) ? 1 : (edge & A8_UP) ? -1 : 0;
        if (dx || dy) a8_osk_move(dx, dy);
        /* Typing: the game runs while fire holds the key down (at least
         * KEY_DOWN_FRAMES) and KEY_UP_FRAMES after, so it sees a whole
         * press and release; otherwise it stays paused. */
        static const int KEY_DOWN_FRAMES = 4, KEY_UP_FRAMES = 4;
        static int down_left = 0, up_left = 0, key = -1;
        static bool fire_was = false;
        bool fire_edge = in.trig[0] && !fire_was;
        fire_was = in.trig[0];
        if (fire_edge && a8_osk_key() == A8_OSK_DRIVE) {    /* DRV: not an Atari key */
            bool on = a8_toggle_drive1();
            a8_display_toast(on ? "DRIVE ON" : "DRIVE OFF");
            in.trig[0] = 0;
            if (a8_settings.keyboard == A8_KBD_ONE_KEY) a8_osk_close();
        }
        if (key < 0 && in.trig[0] && a8_osk_key() != A8_OSK_DRIVE) {   /* a new key */
            key = a8_osk_key();
            down_left = KEY_DOWN_FRAMES;
            up_left = KEY_UP_FRAMES;
        }
        if (key >= 0) {
            if (in.trig[0] || down_left > 0) {         /* down */
                in.key = key;
                if (down_left > 0) down_left--;
            } else if (up_left > 0) {                  /* up, the game still runs */
                up_left--;
            } else {
                key = -1;                              /* done: pause again ... */
                if (a8_settings.keyboard == A8_KBD_ONE_KEY)
                    a8_osk_close();                    /* ... or close, one key typed */
            }
        }
        osk_running = key >= 0;
        in.stick[0] = 0;
        in.trig[0] = 0;
        in.pot[0] = in.pot[1] = A8_POT_NONE;
        in.shift = 0;
    } else {
        osk_running = false;
    }
    a8_input = in;


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
    uint8_t d = port_dirs(0) | joycon_read();
    if ((d & A8_UP)   || low(PIN_SIDE_UP))   b |= A8M_UP;
    if ((d & A8_DOWN) || low(PIN_SIDE_DOWN)) b |= A8M_DOWN;
    if (d & A8_LEFT)  b |= A8M_LEFT;
    if (d & A8_RIGHT) b |= A8M_RIGHT;
    if (port_fire(0) || low(PIN_RED) || low(PIN_CLICK) || low(PIN_START)) b |= A8M_OK;
    if (low(PIN_MENU)) b |= A8M_BACK;
    if (low(PIN_SELECT)) b |= A8M_ALT;
    return b;
}

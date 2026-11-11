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
 *   Green / Yellow    OPTION / SPACE (2.1: Green = fire, Yellow = nothing -
 *                     the defaults; Game Settings maps every button)
 *   START / SELECT    START / SELECT (GPIO 20 / 19, as in the arcade)
 *   MENU              hold 1 s in a game: back to the game list
 *   Side buttons      game list: up / down; in a game, right = RETURN
 *   Yellow + click    the Joy-Con as paddle 1 / as joystick 1, remembered
 *                     for the game: as a paddle its x is the knob (centre =
 *                     the middle; Settings > Paddle sensitivity), Red /
 *                     click its button. (Real paddles on port 1 come first.)
 *   Paddles           ports 1-2 (pico_paddles.cpp): the 400/800/XL's pots
 *                     0-3; their buttons are the ports' left / right lines
 *
 * Duo 5200 Ultimate (PICO_5200, GameAdapterRGBPico data/duo5200.json): the
 * same device buttons and Joy-Con, but the ports are two Atari 5200
 * controllers instead of DB9 joysticks, and there are no side buttons:
 *
 *   5200 controllers  joysticks 1-2 (pico_5200ctl.cpp): the analog stick
 *                     as directions; Fire A or Fire B = fire; keypad 0-9 =
 *                     the number keys, START = START
 *
 * Duo Ultimate (PICO_DUO, GameAdapterRGBPico data/duoultimate.json; the
 * 160x80 test build): two DB9 ports and the device's own controls, no
 * Joy-Con:
 *
 *   Ports 1-2 (DB9)   Atari joysticks 1-2; paddles (pico_paddles.cpp)
 *   5-way stick       also joystick 1; its press = fire 1 (as the Quad's
 *                     Joy-Con click; RETURN is on the on-screen keyboard)
 *   Red               fire 1
 *   Black / White     START / SELECT
 *   Green / Yellow    OPTION / SPACE;  Blue  on-screen keyboard
 *                     (2.1 defaults: Green fire, Yellow nothing)
 *   MENU              tap in a game: the next Scale mode; hold 1 s: back
 *                     to the game list
 *
 * A 5200 game (any device; a8_is_5200) has its own controls:
 *
 *   Joy-Con stick     controller 1's analog stick (POKEY pots 0/1)
 *   Red / click       bottom fire;  Green  top fire
 *   START / SELECT    START / PAUSE;  Yellow  * (2.1: nothing by default)
 *   Blue              the keypad on screen: 0-9 * # START PAUSE RESET
 *   5200 controllers  (Duo) the real thing: analog sticks, bottom (A) and
 *                     top (B) fire, keypads (START PAUSE RESET too). The
 *                     Joy-Con, once moved, stands in for controller 1's stick.
 *   Paddles           (Quad, ports 1-2) a pair stands in for that port's
 *                     5200 controller: paddle A = the stick's x, B = its y
 *                     (turned right = right / down); A's button = bottom
 *                     fire, B's = top fire.
 */
#include <Arduino.h>
#include "a8_input.h"
#include "a8_platform.h"
#include "a8_display.h"
#include "a8_osk.h"
#include "a8_settings.h"
#include "pico_5200ctl.h"
#include "pico_paddles.h"

extern "C" {
#include "akey.h"
}

#ifdef PICO_5200
static const int NUM_PORTS = 2;
static const int PIN_SIDE_UP = -1, PIN_SIDE_DOWN = -1;              /* none */
#elif defined(PICO_DUO)
static const int NUM_PORTS = 2;
static const int8_t PORT_PINS[2][5] = {     /* up down left right fire */
    { 16, 17, 18, 19, 13 },
    { 26, 27, 34, 35, 36 },
};
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
#ifdef PICO_DUO
static const int PIN_RED = 20, PIN_GREEN = 9, PIN_BLUE = 10, PIN_YELLOW = 21;
static const int PIN_SELECT = 24, PIN_START = 22, PIN_MENU = 23, PIN_CLICK = -1;   /* White, Black */
static const int PIN_JOY_X = -1, PIN_JOY_Y = -1;                     /* no Joy-Con */
static const int8_t PIN_5WAY[4] = { 25, 8, 12, 6 };                  /* up down left right */
static const int PIN_RETURN = 7;                                     /* the 5-way's press */
#else
static const int PIN_RED = 14, PIN_GREEN = 15, PIN_BLUE = 16, PIN_YELLOW = 17;
static const int PIN_SELECT = 19, PIN_START = 20, PIN_MENU = 18, PIN_CLICK = 21;
static const int PIN_JOY_X = 44, PIN_JOY_Y = 45;
static const int8_t PIN_5WAY[4] = { -1, -1, -1, -1 };
static const int PIN_RETURN = PIN_SIDE_DOWN;                         /* the right side button */
#endif

/* Game Settings' buttons (A8_GB_*, a8_settings.h): what each one does is
 * per game (a8_settings_game_action). The "click" slot is the Duo's 5-way
 * press (no Joy-Con there); the right side button is the Quad's RETURN. */
#ifdef PICO_DUO
static const int PIN_SLOT_CLICK = PIN_RETURN;
#else
static const int PIN_SLOT_CLICK = PIN_CLICK;
#endif
static const int SLOT_PINS[A8_GB_COUNT] = { PIN_RED, PIN_GREEN, PIN_YELLOW, PIN_SLOT_CLICK,
                                            PIN_SIDE_UP, PIN_SIDE_DOWN, PIN_BLUE };

/* ---- Joy-Con ---------------------------------------------------------
 * The centre is assumed at the first reading (averaged at start-up); as the
 * stick moves, the lowest and highest readings are kept, and once it has
 * gone well past the centre BOTH ways the centre becomes their midpoint
 * (one side alone would skew it). A direction counts outside a dead zone
 * of 30 % of the half-range around the centre (left / right: 65 % in the
 * menus); until both sides are known,
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
    if (a.pin < 0) return a.first;           /* none: always at rest */
    int v = analogRead(a.pin);
    if (v < a.lo) a.lo = v;
    if (v > a.hi) a.hi = v;
    return v;
}

static int8_t axis_dir(const JoyAxis &a, int v, int dead_pct = 30) {
    int centre, half;
    axis_centre(a, &centre, &half);
    int dead = half * dead_pct / 100;
    if (v < centre - dead) return -1;
    if (v > centre + dead) return 1;
    return 0;
}

/* As a 5200 pot: 6 (left / up) .. 114 (centre) .. 220 (right / down), the
 * stick's position as it is - no dead zone: a game reads every bit of
 * movement, as from a real 5200 stick. (It had an 8 % dead zone, returning
 * A8_POT_NONE inside it; dead zones are for the menus and the keyboard,
 * which use the directions, joycon_read's dead_x_pct.) */
static const int POT_MIN = 6, POT_CENTRE = 114, POT_MAX = 220;
static int16_t axis_pot(const JoyAxis &a, int v) {
    int centre, half;
    axis_centre(a, &centre, &half);
    int d = v - centre;
    int p = d > 0 ? POT_CENTRE + d * (POT_MAX - POT_CENTRE) / half
                  : POT_CENTRE + d * (POT_CENTRE - POT_MIN) / half;
    if (p < POT_MIN) p = POT_MIN;
    if (p > POT_MAX) p = POT_MAX;
    return (int16_t)p;
}

static inline bool low(int p) { return p >= 0 && digitalRead(p) == LOW; }

/* The Duo's 5-way as joystick directions (none elsewhere). */
static uint8_t five_way(void) {
    uint8_t d = 0;
    if (low(PIN_5WAY[0])) d |= A8_UP;
    if (low(PIN_5WAY[1])) d |= A8_DOWN;
    if (low(PIN_5WAY[2])) d |= A8_LEFT;
    if (low(PIN_5WAY[3])) d |= A8_RIGHT;
    return d;
}


void a8_input_init(void) {
#ifdef PICO_DUO
    /* The Duo's DB9 pins 7 and 8 are switched (GameAdapterRGBPico main.cpp,
     * docs/PIN7_PIN8_CONTROL_DESIGN.md); put them in Atari mode, pin 7 =
     * +5 V (the paddles' supply) and pin 8 = ground (the joysticks' common).
     * Order matters: the ground switch (GPIO29) opens before the +5 V FETs
     * (GPIO11 low) turn on, and pin 8's +5 V side (GPIO31) is off before
     * its ground side (GPIO30) closes - either pair on at once is a short. */
    digitalWrite(29, LOW);  pinMode(29, OUTPUT); digitalWrite(29, LOW);
    digitalWrite(11, LOW);  pinMode(11, OUTPUT); digitalWrite(11, LOW);
    digitalWrite(31, LOW);  pinMode(31, OUTPUT); digitalWrite(31, LOW);
    pinMode(30, OUTPUT);    digitalWrite(30, HIGH);
#endif
#ifdef PICO_5200
    ctl5200_init();
#else
    for (auto &port : PORT_PINS)
        for (int8_t p : port) pinMode(p, INPUT_PULLUP);
    paddles_init();
#endif
    const int buttons[] = { PIN_RED, PIN_GREEN, PIN_BLUE, PIN_YELLOW, PIN_SELECT,
                            PIN_START, PIN_MENU, PIN_CLICK, PIN_SIDE_UP, PIN_SIDE_DOWN,
                            PIN_5WAY[0], PIN_5WAY[1], PIN_5WAY[2], PIN_5WAY[3], PIN_RETURN };
    for (int p : buttons) if (p >= 0) pinMode(p, INPUT_PULLUP);
    if (PIN_JOY_X < 0) { axis_init(joy_x, 2048); axis_init(joy_y, 2048); return; }
    /* Joy-Con: a first guess at the centre; it keeps learning (axis_sample) */
    analogReadResolution(12);
    delay(20);                               /* let the ADC settle */
    long sx = 0, sy = 0;
    for (int i = 0; i < 32; i++) { sx += analogRead(PIN_JOY_X); sy += analogRead(PIN_JOY_Y); delay(1); }
    axis_init(joy_x, (int)(sx / 32));
    axis_init(joy_y, (int)(sy / 32));
}

/* The Joy-Con's x as paddle 1's pot: the centre is the paddle's middle,
 * full left 228, full right 1 - at Medium sensitivity and below over the
 * whole travel, High and Very high reaching the ends sooner (as a real
 * paddle: 50 % of its turn at Medium, 40, 30). */
static int16_t joycon_paddle(void) {
    static const int GAIN_PCT[A8_PADDLES_LEVELS] = { 100, 100, 100, 125, 167 };
    int v = axis_sample(joy_x), centre, half;
    axis_centre(joy_x, &centre, &half);
    /* Game Settings > Paddle center: the rest position, moved (+ = right) */
    int p = 114 - a8_settings_game_paddle_ofs()
            - (v - centre) * 113 * GAIN_PCT[a8_settings.paddles] / 100 / half;
    return (int16_t)(p < 1 ? 1 : p > 228 ? 228 : p);
}

/* One Joy-Con reading: its directions, and (pot_x/pot_y given) its
 * position as 5200 pots. */
static uint8_t joycon_read(int16_t *pot_x = nullptr, int16_t *pot_y = nullptr, int dead_x_pct = 30) {
    int vx = axis_sample(joy_x), vy = axis_sample(joy_y);
    if (pot_x) *pot_x = axis_pot(joy_x, vx);
    if (pot_y) *pot_y = axis_pot(joy_y, vy);
    int8_t x = axis_dir(joy_x, vx, dead_x_pct), y = axis_dir(joy_y, vy);
    uint8_t d = 0;
    if (x < 0) d |= A8_LEFT;
    if (x > 0) d |= A8_RIGHT;
    if (y < 0) d |= A8_UP;
    if (y > 0) d |= A8_DOWN;
    return d;
}

#ifdef PICO_5200
/* the analog stick as a joystick: past `edge` of its travel (pot units,
 * of ~107 each way) - about a third in a game, 70 % in the menus */
static uint8_t port_dirs(int n, int edge = 36) {
    int16_t x = ctl5200_pot(n, 0), y = ctl5200_pot(n, 1);
    if (x < 0) return 0;
    uint8_t d = 0;
    if (x < 114 - edge) d |= A8_LEFT;
    if (x > 114 + edge) d |= A8_RIGHT;
    if (y < 114 - edge) d |= A8_UP;
    if (y > 114 + edge) d |= A8_DOWN;
    return d;
}
static const int MENU_EDGE = 75;
static bool port_fire(int n) { return ctl5200_fire_a(n) || ctl5200_fire_b(n); }
static bool port_fire_a(int n) { return ctl5200_fire_a(n); }
static bool port_fire_b(int n) { return ctl5200_fire_b(n); }
static void ports_poll(void) { ctl5200_poll(); }
/* keypad key (ctl5200_key) as the 5200's and as the 800's key */
static const int KEYPAD_5200[15] = {
    AKEY_5200_0, AKEY_5200_1, AKEY_5200_2, AKEY_5200_3, AKEY_5200_4, AKEY_5200_5, AKEY_5200_6,
    AKEY_5200_7, AKEY_5200_8, AKEY_5200_9, AKEY_5200_ASTERISK, AKEY_5200_HASH,
    AKEY_5200_START, AKEY_5200_PAUSE, AKEY_5200_RESET };
static const int KEYPAD_800[12] = {
    AKEY_0, AKEY_1, AKEY_2, AKEY_3, AKEY_4, AKEY_5, AKEY_6, AKEY_7, AKEY_8, AKEY_9, -1, -1 };
#else
static uint8_t port_dirs(int n, int edge = 0) {
    (void)edge;
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
static void ports_poll(void) { paddles_poll(); }
static const int MENU_EDGE = 0;
#endif

/* ---- in a game ------------------------------------------------------- */

static uint32_t menu_since = 0;           /* when MENU went down (if menu_down) */
static bool menu_down = false, menu_seen_up = false;

static bool osk_running = false;
static bool game_cfg_wanted = false;      /* the keyboard's gear key: Game Settings */

/* The device buttons held now, through the game's Game Settings. Yellow and
 * the click count for nothing while they're the Joy-Con switch (skip_yc). */
struct Mapped { bool fire, top, start, select, option, keyboard, scale; int key; uint8_t j2; };
static Mapped mapped_buttons(bool is5200, bool skip_yc) {
    Mapped m = { false, false, false, false, false, false, false, -1, 0 };
    for (int slot = 0; slot < A8_GB_COUNT; slot++) {
        if (!low(SLOT_PINS[slot])) continue;
        if (skip_yc && (slot == A8_GB_YELLOW || slot == A8_GB_CLICK)) continue;
        uint8_t a = a8_settings_game_action(slot, is5200);
        switch (a) {
        case A8_ACT_NONE:     break;
        case A8_ACT_FIRE:     m.fire = true; break;
        case A8_ACT_TOP_FIRE: m.top = true; break;
        case A8_ACT_START:    m.start = true; break;
        case A8_ACT_SELECT:   m.select = true; break;
        case A8_ACT_OPTION:   m.option = true; break;
        case A8_ACT_KEYBOARD: m.keyboard = true; break;     /* a press: open / close */
        case A8_ACT_SCALE:    m.scale = true; break;        /* a press: the next mode */
        case A8_ACT_J2_UP:    m.j2 |= A8_UP; break;          /* joystick 2 (twin-stick) */
        case A8_ACT_J2_DOWN:  m.j2 |= A8_DOWN; break;
        case A8_ACT_J2_LEFT:  m.j2 |= A8_LEFT; break;
        case A8_ACT_J2_RIGHT: m.j2 |= A8_RIGHT; break;
        default:              if (a < 0x40) m.key = a; break;   /* an AKEY_* key */
        }
    }
    return m;
}

bool a8_input_take_game_settings(void) {
    bool w = game_cfg_wanted;
    game_cfg_wanted = false;
    return w;
}

bool a8_input_has_slot(int slot) {
    return slot >= 0 && slot < A8_GB_COUNT && SLOT_PINS[slot] >= 0;
}

bool a8_input_has_joycon(void) { return PIN_JOY_X >= 0; }

bool a8_input_osk_paused(void) {
    return a8_osk_is_open() && !osk_running && a8_settings.keyboard != A8_KBD_KEEP_RUNNING;
}

bool a8_input_frame(void) {
    a8_input_t in;
    memset(&in, 0, sizeof(in));
    for (int n = 0; n < 8; n++) in.pot[n] = A8_POT_NONE;
    in.key = -1;
    uint8_t nav;                          /* moves the on-screen keyboard's cursor */
    bool kb_level = false, scale_level = false;   /* buttons mapped to Keyboard / Next scale */
    ports_poll();
#ifdef PICO_5200
    int pad = ctl5200_key();
#endif
    if (a8_is_5200()) {
        /* the Joy-Con is controller 1's analog stick; a controller's own
         * directions count while the Joy-Con rests (input.c) */
        for (int n = 0; n < NUM_PORTS; n++) {
            in.stick[n] = port_dirs(n);
            in.trig[n] = port_fire_a(n);
            if (port_fire_b(n)) in.shift = 1;
        }
#ifdef PICO_5200
        for (int n = 0; n < 2; n++) {         /* the controllers' own sticks */
            in.pot[2 * n] = ctl5200_pot(n, 0);
            in.pot[2 * n + 1] = ctl5200_pot(n, 1);
        }
        if (pad >= 0) in.key = KEYPAD_5200[pad];
#else
        for (int n = 0; n < 2; n++) {         /* paddles as a 5200 controller */
            if (!paddles_present(n)) continue;
            for (int a = 0; a < 2; a++) {
                int16_t p = paddles_pot(n, a);   /* 1 (right) .. 228 */
                in.pot[2 * n + a] = p > 0 ? (int16_t)(POT_MIN + (228 - p) * (POT_MAX - POT_MIN) / 227) : A8_POT_NONE;
            }
            /* their buttons are the left / right lines, not directions */
            in.trig[n] = (in.stick[n] & A8_LEFT) != 0;
            if (in.stick[n] & A8_RIGHT) in.shift = 1;
            in.stick[n] = 0;
        }
#endif
        /* the Joy-Con is controller 1's analog stick, with no dead zone (its
         * position as it is, axis_pot). Whichever was moved last steers: a
         * real controller 1 (Duo 5200) or a digital direction (a joystick on
         * port 1, the Duo's 5-way) takes over while the Joy-Con rests, and
         * the Joy-Con takes it back once it's clearly pushed (past 15 % -
         * only to tell a push from a resting stick's wander; not a dead
         * zone: from then on every bit of its movement counts). (It used to
         * count only while pushed past 15 %: a dead zone in the game.) */
        in.stick[0] |= five_way();
        int16_t jx, jy;
        nav = in.stick[0] | joycon_read(&jx, &jy);
        bool pushed = jx < 114 - 16 || jx > 114 + 16 || jy < 114 - 16 || jy > 114 + 16;
        bool c1_rests = (in.pot[0] == A8_POT_NONE || in.pot[0] == 114) &&
                        (in.pot[1] == A8_POT_NONE || in.pot[1] == 114);
        bool other_steers = !c1_rests || (in.stick[0] & (A8_UP | A8_DOWN | A8_LEFT | A8_RIGHT));
        static bool joy_steers = true;
        if (pushed) joy_steers = true;
        else if (other_steers) joy_steers = false;
        if (joy_steers && PIN_JOY_X >= 0) {
            in.pot[0] = jx;
            in.pot[1] = jy;
        }
        /* Game Settings > Horizontal center: controller 1's stick x, moved
         * (+ = right) - at rest too, where the core would read 114. Not
         * while a direction is held without an analog value (the Duo's
         * 5-way): the core reads those only when both pots are at 114. */
        int hofs = a8_settings_game_paddle_ofs();
        if (hofs) {
            if (in.pot[0] != A8_POT_NONE) {
                int x = in.pot[0] + hofs;
                in.pot[0] = (int16_t)(x < POT_MIN ? POT_MIN : x > POT_MAX ? POT_MAX : x);
            } else if (!(in.stick[0] & (A8_UP | A8_DOWN | A8_LEFT | A8_RIGHT))) {
                int x = 114 + hofs;
                in.pot[0] = (int16_t)(x < POT_MIN ? POT_MIN : x > POT_MAX ? POT_MAX : x);
            }
        }
        /* Red / click (Duo: the 5-way's press): bottom fire; Green: top
         * fire; Yellow: * - unless the game's Game Settings say otherwise
         * (2.1 defaults: Yellow nothing) */
        Mapped m = mapped_buttons(true, false);
        if (m.fire) in.trig[0] = 1;
        if (m.top) in.shift = 1;
        if (m.key >= 0) in.key = m.key;
        if (m.j2) {                           /* controller 2's stick to the edges: */
            in.stick[1] |= m.j2;              /* its directions, which the core reads */
            in.pot[2] = in.pot[3] = A8_POT_NONE;   /* when the pots are at rest */
        }
        kb_level = m.keyboard;
        scale_level = m.scale;
        if (low(PIN_SELECT)) in.key = AKEY_5200_PAUSE;
        if (low(PIN_START))  in.key = AKEY_5200_START;
    } else {
        for (int n = 0; n < NUM_PORTS; n++) {
            in.stick[n] = port_dirs(n);
            in.trig[n] = port_fire(n);
        }
#ifndef PICO_5200
        for (int n = 0; n < 2; n++)           /* paddles: A, B per port */
            for (int a = 0; a < 2; a++) in.pot[2 * n + a] = paddles_pot(n, a);
#else
        if (pad >= 0 && pad < 12) in.key = KEYPAD_800[pad];
        if (pad == 12) in.start = 1;
#endif
        /* Yellow + click: the Joy-Con as paddle 1 or joystick 1 (both
         * buttons count for nothing else until they're let go) */
        static bool combo = false;
        bool yellow = low(PIN_YELLOW), click = low(PIN_CLICK);
        if (yellow && click && !combo) {
            bool on = !a8_settings_game_paddle();
            a8_settings_set_game_paddle(on);
            a8_display_toast(on ? "PADDLE" : "JOYSTICK");   /* (11 characters at most) */
        }
        if (yellow && click) combo = true;
        else if (!yellow && !click) combo = false;
        bool joy_paddle = a8_settings_game_paddle();
#ifdef PICO_QUAD
        if (paddles_present(0)) joy_paddle = false;     /* the real ones */
#endif
        uint8_t joy = joycon_read() | five_way();
        nav = in.stick[0] | joy;
        /* Red / click: fire; Green: OPTION; Yellow: SPACE; right side
         * (2.1 defaults: Green fire, Yellow nothing) -
         * button: RETURN (Duo: the 5-way's press is fire) - unless the
         * game's Game Settings say otherwise */
        Mapped m = mapped_buttons(false, combo);
        bool fire = m.fire;
        if (joy_paddle && !a8_osk_is_open()) {
            in.pot[0] = joycon_paddle();
            if (fire) in.stick[0] |= A8_LEFT;           /* paddle 1's button */
        } else {
            in.stick[0] |= joy;
            if (fire) in.trig[0] = 1;
        }
        in.start  |= low(PIN_START) || m.start;
        in.select = low(PIN_SELECT) || m.select;
        in.option = m.option;
        if (m.key >= 0) in.key = m.key;
        in.stick[1] |= m.j2;                  /* joystick 2 (twin-stick games) */
        kb_level = m.keyboard;
        scale_level = m.scale;

        /* Right side button (Duo: the 5-way's press): RETURN. On the Duo the
         * press is fire 1 instead — a press on a stick is expected to fire
         * (it was RETURN, which pauses many games); RETURN is on the keyboard. */
        /* (Both are now the defaults of Game Settings' slots, mapped above.) */
    }

    /* Blue: the on-screen keyboard, open / close on each press. (It used to
     * be RETURN on a tap as well, and a short first press typed a RETURN
     * before the keyboard opened.) */
    /* (Now: any button mapped to Keyboard - Blue's default, Game Settings -
     * and a MENU tap, below.) */
    static bool blue_was = true;          /* one still held from the menu: ignored */
    uint32_t t = millis();
    bool blue = kb_level;
    if (blue && !blue_was) {
        if (a8_osk_is_open()) a8_osk_close();
        else a8_osk_open();
    }
    blue_was = blue;
    /* Next scale (a mapped button): the game's own Scale, the next mode */
    static bool scale_was = true;
    if (scale_level && !scale_was) {
        a8_settings_next_scale();
        a8_display_toast(a8_screen_scale_names[a8_settings_scale_now()]);
    }
    scale_was = scale_level;

    /* The keyboard takes joystick 1 and its fire: the stick moves the
     * cursor (auto-repeat), fire types the key (held while fire is). */
    if (a8_osk_is_open()) {
        /* Red and the click type a key whatever the game maps them to
         * (Game Settings), so a remapped Red can't lock the keyboard */
        if (low(PIN_RED) || low(PIN_CLICK) || low(PIN_SLOT_CLICK)) in.trig[0] = 1;
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
            /* (no longer on the keyboard: Game Settings' Disk drive 1) */
            bool on = a8_toggle_drive1();
            a8_display_toast(on ? "DRIVE ON" : "DRIVE OFF");
            in.trig[0] = 0;
            if (a8_settings.keyboard == A8_KBD_ONE_KEY) a8_osk_close();
        }
        if (fire_edge && a8_osk_key() == A8_OSK_RESET) {    /* RST: the Atari's RESET key */
            a8_request_warmstart();                         /* once (a8_platform.c) */
            a8_display_toast("RESET");
            in.trig[0] = 0;
            a8_osk_close();                                 /* the game runs it at once */
        }
        if (fire_edge && a8_osk_key() == A8_OSK_GAME) {     /* the gear: Game Settings */
            game_cfg_wanted = true;                         /* main.cpp opens it */
            in.trig[0] = 0;
            a8_osk_close();
        }
        if (key < 0 && in.trig[0] && a8_osk_key() >= 0) {  /* a new key (not DRV / RST / the gear) */
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
     * started (from the menu) counts only after it has been released.
     * 160x80 panel: a shorter press (a tap) picks the next Scale mode.
     * (Now, on every device, a tap opens / closes the on-screen keyboard,
     * so Blue is free to map; Scale is the Next scale action and Game
     * Settings > Scale.) */
    bool m = low(PIN_MENU);
    if (!m) {
        if (menu_seen_up && menu_down && millis() - menu_since < 1000) {
            if (a8_osk_is_open()) a8_osk_close();
            else a8_osk_open();
            Serial.printf("[input] MENU tap (%lu ms): keyboard %s\n",
                          (unsigned long)(millis() - menu_since), a8_osk_is_open() ? "open" : "closed");
        }
        menu_seen_up = true; menu_down = false; return false;
    }
    if (!menu_seen_up) return false;
    uint32_t now = millis();
    /* (menu_since used to be `now | 1`, non-zero as the "down" mark: on an
     * even millisecond that is 1 ms ahead, now - menu_since wrapped to
     * 4294967295 and a tap left the game) */
    if (!menu_down) { menu_down = true; menu_since = now; }
    if (now - menu_since < 1000) return false;
    Serial.printf("[input] MENU held %lu ms: leaving the game\n", (unsigned long)(now - menu_since));
    return true;
}

/* ---- game list ------------------------------------------------------- */

static const int MENU_JOY_DEAD_X = 65;     /* % of the half-range (30 in a game) */

uint8_t a8_input_menu(void) {
    uint8_t b = 0;
    ports_poll();
    /* left / right change values in Settings: the Joy-Con needs a firm push */
    uint8_t d = port_dirs(0, MENU_EDGE) | joycon_read(nullptr, nullptr, MENU_JOY_DEAD_X) | five_way();
    if ((d & A8_UP)   || low(PIN_SIDE_UP))   b |= A8M_UP;
    if ((d & A8_DOWN) || low(PIN_SIDE_DOWN)) b |= A8M_DOWN;
    if (d & A8_LEFT)  b |= A8M_LEFT;
    if (d & A8_RIGHT) b |= A8M_RIGHT;
    if (port_fire(0) || low(PIN_RED) || low(PIN_CLICK) || low(PIN_START) ||
        (PIN_RETURN != PIN_SIDE_DOWN && low(PIN_RETURN))) b |= A8M_OK;    /* Duo: the 5-way's press */
    if (low(PIN_MENU)) b |= A8M_BACK;
    if (low(PIN_SELECT)) b |= A8M_ALT;
    if (low(PIN_GREEN)) b |= A8M_GREEN;
    return b;
}

/* ---- controller test (a8_ctltest.cpp) ---------------------------------- */

void a8_input_joycon_diag(int raw[2], int centre[2], int lo[2], int hi[2], int16_t pot[2]) {
    JoyAxis *axes[2] = { &joy_x, &joy_y };
    for (int a = 0; a < 2; a++) {
        raw[a] = axis_sample(*axes[a]);
        int half;
        axis_centre(*axes[a], &centre[a], &half);
        lo[a] = axes[a]->lo;
        hi[a] = axes[a]->hi;
        pot[a] = axis_pot(*axes[a], raw[a]);
    }
}

bool a8_input_button(int which) {
    static const int PINS[A8B_COUNT] = { PIN_RED, PIN_GREEN, PIN_BLUE, PIN_YELLOW,
                                         PIN_START, PIN_SELECT, PIN_CLICK, PIN_MENU,
                                         PIN_SIDE_UP, PIN_SIDE_DOWN };
    return which >= 0 && which < A8B_COUNT && low(PINS[which]);
}

uint8_t a8_input_port_dirs(int n, bool *fire) {
    ports_poll();
    if (n < 0 || n >= NUM_PORTS) { *fire = false; return 0; }
    *fire = port_fire(n);
    return port_dirs(n);
}

int a8_input_num_ports(void) { return NUM_PORTS; }

uint8_t a8_input_five_way(bool *press) {
    *press = PIN_RETURN != PIN_SIDE_DOWN && low(PIN_RETURN);
    return five_way();
}

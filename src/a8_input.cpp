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
 * Adafruit Fruit Jam (PICO_FRUITJAM, the headless sound test): only its
 * three buttons - 1 fire, 2 START, 3 the next game on the drive.
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
#include "pico_audio.h"                    /* the menus' sound; the serial monitor's sound keys */
#ifdef PICO_FRUITJAM
#include "a8_display.h"                    /* V: the next HDMI mode */
#endif

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
#elif defined(PICO_FRUITJAM)
/* Adafruit Fruit Jam: no joystick port. Port 1 is the serial monitor's
 * "virtual controller" (virtual pins from VPIN_BASE: no GPIO, see low() and
 * serial_poll below), so the loops over the ports stay as they are. Its
 * GPIO 24-39 are I2S, SD card and NeoPixels here, 12-21 HDMI and the
 * codec's I2C. */
enum { VPIN_BASE = 48,                      /* above the RP2350B's GPIO 0-47 */
       VP_UP = VPIN_BASE, VP_DOWN, VP_LEFT, VP_RIGHT, VP_FIRE, VP_SELECT, VP_OPTION, VP_BLUE,
       VP_GREEN, VP_YELLOW, VP_SIDE_L, VP_SIDE_R,  /* a USB pad's B, X, L, R (A8_USB_HOST) */
       VPIN_END };
static const int NUM_PORTS = 1;
static const int8_t PORT_PINS[1][5] = {     /* up down left right fire */
    { VP_UP, VP_DOWN, VP_LEFT, VP_RIGHT, VP_FIRE },
};
#ifdef A8_USB_HOST
/* a USB pad's L / R: Game Settings' side-button slots */
static const int PIN_SIDE_UP = VP_SIDE_L, PIN_SIDE_DOWN = VP_SIDE_R;
#else
static const int PIN_SIDE_UP = -1, PIN_SIDE_DOWN = -1;              /* none */
#endif
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
#elif defined(PICO_FRUITJAM)
/* The Fruit Jam's three buttons (active LOW, pulled up): button 1 (GPIO 0,
 * also BOOT) = fire, button 2 (GPIO 4) = START, button 3 (GPIO 5) = MENU
 * (tap: keyboard, hold: the game list). In the game list (a8_input_menu):
 * 1 down, 3 tap up, 2 OK, 3 held back. Headless (no list): button 3 = the
 * next game on the drive instead (a8_input_next_game, main.cpp). SELECT,
 * OPTION and Blue (the keyboard) only from the serial monitor. No Joy-Con. */
#ifdef A8_USB_HOST
/* With the USB host, a pad's A, B, X, Y, L, R are Game Settings' slots
 * (Red - with button 1 -, Green, Yellow, Blue, side L, side R): mapped per
 * game like the other devices' buttons. */
static const int PIN_RED = 0, PIN_GREEN = VP_GREEN, PIN_BLUE = VP_BLUE, PIN_YELLOW = VP_YELLOW;
#else
static const int PIN_RED = 0, PIN_GREEN = -1, PIN_BLUE = VP_BLUE, PIN_YELLOW = -1;
#endif
#ifdef A8_HEADLESS
static const int PIN_SELECT = VP_SELECT, PIN_START = 4, PIN_MENU = -1, PIN_CLICK = -1;
static const int PIN_NEXT_GAME = 5;
#else
static const int PIN_SELECT = VP_SELECT, PIN_START = 4, PIN_MENU = 5, PIN_CLICK = -1;
#endif
static const int PIN_JOY_X = -1, PIN_JOY_Y = -1;
static const int8_t PIN_5WAY[4] = { -1, -1, -1, -1 };
static const int PIN_RETURN = PIN_SIDE_DOWN;                         /* none */
/* (PIN_RETURN == PIN_SIDE_DOWN reads as "none"; with the USB host
 * PIN_SIDE_DOWN is the pad's R) */
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

#ifdef PICO_FRUITJAM
/* ---- the serial monitor as a controller (Fruit Jam) ----------------------
 * A terminal sends key presses, not releases: a key holds its (virtual or
 * real) pin down for a short while, kept down by the terminal's key repeat.
 *   arrows / w a s d   joystick 1 (game list: up / down)
 *   space              fire (game list: OK)
 *   Enter              START (button 2; game list: OK)
 *   Esc / m            MENU held (button 3: back / the game list)
 *   Tab / e            SELECT      o  OPTION      k  the on-screen keyboard
 *   V                  the next HDMI mode     O  sound: Fruit Jam / HDMI
 * Anything else goes to the sound's bench keys (pico_audio_key). */
static uint32_t vpin_until[VPIN_END];      /* indexed by pin: down until this millis() */

static void vpin_press(int p, uint32_t ms) { if (p >= 0 && p < VPIN_END) vpin_until[p] = millis() + ms; }
static bool vpin_down(int p) {
    return p >= 0 && p < VPIN_END && (int32_t)(vpin_until[p] - millis()) > 0;
}

static void serial_key(int c) {
    static const uint32_t TAP = 150, HOLD = 1200;
    switch (c) {
    case 'w': case 'W': vpin_press(VP_UP, TAP); break;
    case 's':           vpin_press(VP_DOWN, TAP); break;
    case 'a':           vpin_press(VP_LEFT, TAP); break;
    case 'd': case 'D': vpin_press(VP_RIGHT, TAP); break;
    case ' ':           vpin_press(VP_FIRE, TAP); break;
    case 13: case 10:   vpin_press(PIN_START, TAP); break;
    case 27: case 'm':  vpin_press(PIN_MENU, HOLD); break;
    case 9: case 'e':   vpin_press(VP_SELECT, TAP); break;
    case 'o':           vpin_press(VP_OPTION, TAP); break;
    case 'k':           vpin_press(VP_BLUE, TAP); break;
#ifdef A8_HDMI
    case 'V':           a8_display_next_hdmi_mode(); break;
#ifdef A8_HDMI_AUDIO
    case 'O':           pico_audio_toggle_hdmi(); break;
#endif
#endif
    default:            pico_audio_key(c); break;
    }
}

static void serial_poll(void) {
    /* arrows come as Esc [ A..D: a lone Esc is MENU once nothing follows */
    static int esc = 0;                    /* 1: Esc seen, 2: Esc [ seen */
    static uint32_t esc_at = 0;
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (esc == 1) {
            if (c == '[') { esc = 2; continue; }
            esc = 0;
            serial_key(27);
        } else if (esc == 2) {
            esc = 0;
            if (c == 'A') vpin_press(VP_UP, 150);
            else if (c == 'B') vpin_press(VP_DOWN, 150);
            else if (c == 'C') vpin_press(VP_RIGHT, 150);
            else if (c == 'D') vpin_press(VP_LEFT, 150);
            continue;                      /* another sequence: ignored */
        }
        if (c == 27) { esc = 1; esc_at = millis(); continue; }
        serial_key(c);
    }
    if (esc == 1 && millis() - esc_at > 30) { esc = 0; serial_key(27); }
}

#ifdef A8_USB_HOST
/* ---- the USB host ports (Fruit Jam) --------------------------------------
 * A USB gamepad and / or keyboard on the Fruit Jam's USB-A ports: TinyUSB's
 * host stack over Pico-PIO-USB (D+ GPIO 1, D- 2; 5 V switched by GPIO 11),
 * run on core 0 (a8_input_usb_task, from the input polling - see there);
 * its reports land here and are read as held buttons - the same virtual pins the serial
 * monitor taps, but held exactly as long as the button or key is (not
 * through the serial port: memory, read every frame).
 *   Gamepad (the Adafruit SNES-style pad, 081F:E401, and pads with its
 *   report - X, Y axes in bytes 0-1, buttons in 5-6; a hat in byte 5's low
 *   nibble, where there is one):
 *     D-pad    joystick 1 / menus: move      Select   SELECT
 *     Start    START                         Start + Select  MENU (tap:
 *     A, B, X, Y, L, R: Game Settings' slots            keyboard, hold: the
 *       Red, Green, Yellow, Blue, side L, side R -       game list)
 *       mapped per game (by default A, B fire, X nothing, Y the on-screen
 *       keyboard, L nothing, R RETURN)
 *     menus: A OK, B back, X Game Settings (the game under the cursor)
 *   A second pad is player 2: its D-pad joystick 2 (a 5200 game:
 *   controller 2), its slots through the same map - fire to joystick 2,
 *   the rest (keys, OPTION ...) as player 1's; its Start / Select START /
 *   SELECT, not MENU. Pads are numbered as they connect.
 *   Keyboard (boot protocol): types into the Atari's keyboard (by character,
 *   as a US PC keyboard; Ctrl: CTRL), and
 *     arrows   joystick 1 (Ctrl + arrows: the Atari's cursor keys)
 *     Left Alt / Right Ctrl   fire (Red's slot)   F1  HELP
 *     F2 OPTION   F3 SELECT   F4 START       F5  RESET (warm)
 *     F12      MENU                          Home  CLEAR
 *     menus: arrows move, Enter OK, Esc / Backspace back
 *   A 5200 game: digits are the keypad, * and # (Shift 8 / Shift 3, or the
 *   number pad's), F3 PAUSE, F4 START, F5 RESET.
 * TODO: other pads' layouts (a table by VID:PID, or the report descriptor);
 * Settings > Controller test shows a pad's raw report to find them. */
#include "Adafruit_TinyUSB.h"
#include "pio_usb.h"

static Adafruit_USBH_Host usb_host;
/* written by core 1 (the reports), read by core 0 */
struct UsbPad {
    bool on;
    uint8_t addr, idx;                                   /* its device, HID interface */
    uint16_t vid, pid;
    uint32_t buttons;                                    /* A8U_* held */
    uint8_t raw[16], raw_len;                            /* its last report as it came */
};
static UsbPad usb_pads[2];                               /* player 1, player 2 (as they connect) */
static uint8_t usb_kbd[8];                               /* the last keyboard report */
static bool    usb_kbd_on = false;
static uint8_t usb_kbd_addr = 0;
static uint32_t usb_events = 0;                          /* mounts / unmounts: logged */

/* The SNES-style pad's report (and the many cheap pads that share it). */
static uint32_t usb_parse_pad(const uint8_t *r, uint16_t len) {
    uint32_t b = 0;
    if (len >= 2) {
        if (r[0] < 0x40) b |= A8U_LEFT;  else if (r[0] > 0xC0) b |= A8U_RIGHT;
        if (r[1] < 0x40) b |= A8U_UP;    else if (r[1] > 0xC0) b |= A8U_DOWN;
    }
    if (len >= 6) {
        static const uint32_t HAT[8] = { A8U_UP, A8U_UP | A8U_RIGHT, A8U_RIGHT, A8U_DOWN | A8U_RIGHT,
                                         A8U_DOWN, A8U_DOWN | A8U_LEFT, A8U_LEFT, A8U_UP | A8U_LEFT };
        uint8_t hat = r[5] & 0x0F;
        if (hat < 8) b |= HAT[hat];
        if (r[5] & 0x10) b |= A8U_X;
        if (r[5] & 0x20) b |= A8U_A;
        if (r[5] & 0x40) b |= A8U_B;
        if (r[5] & 0x80) b |= A8U_Y;
    }
    if (len >= 7) {
        if (r[6] & 0x01) b |= A8U_L;
        if (r[6] & 0x02) b |= A8U_R;
        if (r[6] & 0x10) b |= A8U_SELECT;
        if (r[6] & 0x20) b |= A8U_START;
    }
    return b;
}

/* (TinyUSB's header declares the HID mount / unmount callbacks weak, which
 * made these definitions weak too - and the linker took Adafruit's empty
 * ones: the pad was set up but never asked for a report. a8_stubs.c, which
 * doesn't include that header, defines them and calls these.) */
extern "C" void a8_usb_hid_mount(uint8_t dev, uint8_t idx, uint8_t const *desc, uint16_t desc_len) {
    (void)desc; (void)desc_len;
    uint8_t proto = tuh_hid_interface_protocol(dev, idx);
    if (proto == HID_ITF_PROTOCOL_KEYBOARD) {
        usb_kbd_addr = dev;
        usb_kbd_on = true;
    } else if (proto == HID_ITF_PROTOCOL_MOUSE) {
        return;                                          /* not used */
    } else {
        UsbPad *p = !usb_pads[0].on ? &usb_pads[0] : !usb_pads[1].on ? &usb_pads[1] : nullptr;
        if (!p) return;                                  /* a third: not used */
        uint16_t vid = 0, pid = 0;
        tuh_vid_pid_get(dev, &vid, &pid);
        *p = UsbPad();
        p->vid = vid;
        p->pid = pid;
        p->addr = dev;
        p->idx = idx;
        p->on = true;
    }
    usb_events++;
    tuh_hid_receive_report(dev, idx);
}

extern "C" void a8_usb_hid_umount(uint8_t dev, uint8_t idx) {
    for (UsbPad &p : usb_pads)
        if (p.on && p.addr == dev && p.idx == idx) p = UsbPad();
    if (dev == usb_kbd_addr && usb_kbd_on) {
        usb_kbd_on = false;
        for (int i = 0; i < 8; i++) usb_kbd[i] = 0;
    }
    usb_events++;
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev, uint8_t idx, uint8_t const *r, uint16_t len) {
    if (dev == usb_kbd_addr && usb_kbd_on && tuh_hid_interface_protocol(dev, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        for (int i = 0; i < 8; i++) usb_kbd[i] = i < len ? r[i] : 0;
    } else {
        for (UsbPad &p : usb_pads) {
            if (!p.on || p.addr != dev || p.idx != idx) continue;
            uint16_t n = len < sizeof(p.raw) ? len : sizeof(p.raw);
            for (uint16_t i = 0; i < n; i++) p.raw[i] = r[i];
            p.raw_len = (uint8_t)n;
            p.buttons = usb_parse_pad(r, len);
        }
    }
    tuh_hid_receive_report(dev, idx);
}

/* The host stack runs on core 0, as in Adafruit's own Fruit Jam code
 * (Adafruit_Arcade_Machines, src/boards/fruitjam/usb_host_fruitjam.cpp).
 * It ran on core 1 at first, beside the HDMI output, and hung core 1 a
 * fraction of a second after start-up (the on-board hub is always there):
 * Pico-PIO-USB sends each packet from its 1 ms frame timer by starting the
 * DMA and then clearing the "done" flag, and an interrupt in between that
 * outlasts the packet wipes the flag - the transfer then waits for ever,
 * inside the timer interrupt. Core 1's HDMI scanline interrupt is the
 * highest priority there (it must be), so it did exactly that, and with
 * core 1 went the HDMI sound feed (and the emulator, paced by that sound).
 * Here the frame timer (TIMER0 alarm 2: Pico-PIO-USB's alarm pool, made on
 * the core that starts the host) gets the highest priority on core 0, so
 * nothing interrupts a transfer; core 1 has only the HDMI output again.
 * TinyUSB's waits while it sets a device up (debounce 150 ms, resets,
 * retries) spin on core 0: the menu / game holds for them, only then.
 * PIO 2 and DMA channel 11: clear of the HDMI driver's channels 0-3 and the
 * I2S's PIO. */
#include "hardware/irq.h"
#include "hardware/timer.h"
static bool usb_started = false;

/* Core 0, once (a8_input_init). */
static void usb_start(void) {
    {
        pinMode(PIN_5V_EN, OUTPUT);
        digitalWrite(PIN_5V_EN, PIN_5V_EN_STATE);       /* the ports' 5 V */
        pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
        cfg.pin_dp = PIN_USB_HOST_DP;                     /* D- = D+ + 1 */
#ifndef A8_USB_PIO
#define A8_USB_PIO 2                         /* (build-time: the PIO block, DMA channel) */
#endif
#ifndef A8_USB_DMA
#define A8_USB_DMA 11
#endif
        cfg.pio_tx_num = A8_USB_PIO;
        cfg.pio_rx_num = A8_USB_PIO;
        cfg.tx_ch = A8_USB_DMA;
        Serial.printf("[usb] host on PIO %d, DMA channel %d%c", A8_USB_PIO, A8_USB_DMA, 10);
        usb_host.configure_pio_usb(1, &cfg);
        usb_host.begin(1);
        irq_set_priority(timer_hardware_alarm_get_irq_num(timer0_hw, 2), PICO_HIGHEST_IRQ_PRIORITY);
        usb_started = true;
    }
}

/* A flash write (a8_flash.cpp: unpacking a game, saving the settings) runs
 * with interrupts off for tens to hundreds of ms: no USB frames meanwhile,
 * and a USB device suspends after 3 ms of an idle bus. The SNES-style pad
 * then never woke properly (it kept repeating its last report: no button
 * worked in any game - each starts with an unpack - or after a save). So
 * after a write the ports' 5 V goes off for USB_POWER_OFF_MS and back on:
 * to the host, an unplug and a plug, and every device is set up again
 * (about half a second). Several writes in a row: one power-cycle, timed
 * from the last. */
static const uint32_t USB_POWER_OFF_MS = 300;
static bool usb_power_off = false;
static uint32_t usb_power_off_at = 0;

void a8_input_usb_flash_written(void) {
    if (!usb_started) return;
    digitalWrite(PIN_5V_EN, !PIN_5V_EN_STATE);
    usb_power_off = true;
    usb_power_off_at = millis();
}

/* Core 0: the host stack's task (its callbacks run here), from the input
 * polling - the menus' (a8_input_menu) and the game's (a8_input_frame). */
void a8_input_usb_task(void) {
    if (usb_power_off && millis() - usb_power_off_at >= USB_POWER_OFF_MS) {
        digitalWrite(PIN_5V_EN, PIN_5V_EN_STATE);
        usb_power_off = false;
        Serial.printf("[usb] ports powered again (after a flash write)%c", 10);
    }
    if (usb_started) usb_host.task();
}

/* Keyboard: HID usage -> the Atari key, unshifted / with Shift (US layout;
 * -1: none). */
struct UsbKey { int16_t plain, shifted; };
static UsbKey usb_key(uint8_t u) {
    static const int8_t LETTERS[26] = { AKEY_a, AKEY_b, AKEY_c, AKEY_d, AKEY_e, AKEY_f, AKEY_g, AKEY_h,
                                        AKEY_i, AKEY_j, AKEY_k, AKEY_l, AKEY_m, AKEY_n, AKEY_o, AKEY_p,
                                        AKEY_q, AKEY_r, AKEY_s, AKEY_t, AKEY_u, AKEY_v, AKEY_w, AKEY_x,
                                        AKEY_y, AKEY_z };
    static const int16_t DIGITS[10][2] = {               /* 1..9, 0: as the PC's shifted symbols */
        { AKEY_1, AKEY_SHFT | AKEY_1 }, { AKEY_2, AKEY_AT }, { AKEY_3, AKEY_SHFT | AKEY_3 },
        { AKEY_4, AKEY_SHFT | AKEY_4 }, { AKEY_5, AKEY_SHFT | AKEY_5 }, { AKEY_6, AKEY_CARET },
        { AKEY_7, AKEY_SHFT | AKEY_6 }, { AKEY_8, AKEY_ASTERISK }, { AKEY_9, AKEY_SHFT | AKEY_9 },
        { AKEY_0, AKEY_SHFT | AKEY_0 } };
    if (u >= 0x04 && u <= 0x1D) return { LETTERS[u - 0x04], (int16_t)(AKEY_SHFT | LETTERS[u - 0x04]) };
    if (u >= 0x1E && u <= 0x27) return { DIGITS[u - 0x1E][0], DIGITS[u - 0x1E][1] };
    if (u >= 0x59 && u <= 0x62) {                        /* the number pad's 1..9, 0 */
        int16_t k = DIGITS[u - 0x59][0];
        return { k, k };
    }
    switch (u) {
    case 0x28: case 0x58: return { AKEY_RETURN, AKEY_RETURN };
    case 0x29: return { AKEY_ESCAPE, AKEY_ESCAPE };
    case 0x2A: return { AKEY_BACKSPACE, AKEY_BACKSPACE };
    case 0x2B: return { AKEY_TAB, AKEY_SHFT | AKEY_TAB };
    case 0x2C: return { AKEY_SPACE, AKEY_SPACE };
    case 0x2D: return { AKEY_MINUS, AKEY_UNDERSCORE };
    case 0x2E: return { AKEY_EQUAL, AKEY_PLUS };
    case 0x2F: return { AKEY_BRACKETLEFT, AKEY_BRACKETLEFT };
    case 0x30: return { AKEY_BRACKETRIGHT, AKEY_BRACKETRIGHT };
    case 0x31: return { AKEY_BACKSLASH, AKEY_BAR };
    case 0x33: return { AKEY_SEMICOLON, AKEY_COLON };
    case 0x34: return { AKEY_QUOTE, AKEY_DBLQUOTE };
    case 0x35: return { AKEY_ATARI, AKEY_ATARI };        /* ` : the inverse-video key */
    case 0x36: return { AKEY_COMMA, AKEY_LESS };
    case 0x37: return { AKEY_FULLSTOP, AKEY_GREATER };
    case 0x38: return { AKEY_SLASH, AKEY_SHFT | AKEY_SLASH };
    case 0x39: return { AKEY_CAPSTOGGLE, AKEY_CAPSTOGGLE };
    case 0x3A: return { AKEY_HELP, AKEY_HELP };          /* F1 */
    case 0x49: return { AKEY_INSERT_CHAR, AKEY_INSERT_CHAR };
    case 0x4A: return { AKEY_CLEAR, AKEY_CLEAR };        /* Home */
    case 0x4C: return { AKEY_DELETE_CHAR, AKEY_DELETE_CHAR };
    case 0x54: return { AKEY_SLASH, AKEY_SLASH };        /* the number pad's / * - + . */
    case 0x55: return { AKEY_ASTERISK, AKEY_ASTERISK };
    case 0x56: return { AKEY_MINUS, AKEY_MINUS };
    case 0x57: return { AKEY_PLUS, AKEY_PLUS };
    case 0x63: return { AKEY_FULLSTOP, AKEY_FULLSTOP };
    default:   return { -1, -1 };
    }
}

static bool usb_kbd_has(uint8_t usage) {
    for (int i = 2; i < 8; i++) if (usb_kbd[i] == usage) return true;
    return false;
}

/* The keyboard's buttons (A8U_*): arrows, fire, the console keys, MENU. */
static uint32_t usb_kbd_buttons(void) {
    if (!usb_kbd_on) return 0;
    const uint8_t mod = usb_kbd[0];
    uint32_t b = 0;
    if (!(mod & 0x11)) {                                 /* not with Ctrl: the cursor keys */
        if (usb_kbd_has(0x52)) b |= A8U_UP;
        if (usb_kbd_has(0x51)) b |= A8U_DOWN;
        if (usb_kbd_has(0x50)) b |= A8U_LEFT;
        if (usb_kbd_has(0x4F)) b |= A8U_RIGHT;
    }
    if (mod & 0x04 || mod & 0x10) b |= A8U_A;            /* Left Alt, Right Ctrl: fire (Red) */
    if (usb_kbd_has(0x3B)) b |= A8U_OPTION;              /* F2 OPTION */
    if (usb_kbd_has(0x3C)) b |= A8U_SELECT;              /* F3 */
    if (usb_kbd_has(0x3D)) b |= A8U_START;               /* F4 */
    if (usb_kbd_has(0x45)) b |= A8U_MENU;                /* F12 MENU */
    return b;
}

/* The key typed (an AKEY_* code with SHIFT / CTRL), or -1. 5200: the keypad. */
static int usb_kbd_akey(bool is5200) {
    if (!usb_kbd_on) return -1;
    const uint8_t mod = usb_kbd[0];
    const bool shift = mod & 0x22, ctrl = mod & 0x01;    /* (Right Ctrl is fire) */
    for (int i = 2; i < 8; i++) {
        uint8_t u = usb_kbd[i];
        if (!u) continue;
        if (is5200) {
            static const int16_t PAD[10] = { AKEY_5200_1, AKEY_5200_2, AKEY_5200_3, AKEY_5200_4, AKEY_5200_5,
                                             AKEY_5200_6, AKEY_5200_7, AKEY_5200_8, AKEY_5200_9, AKEY_5200_0 };
            if (u >= 0x1E && u <= 0x27) {
                if (shift && u == 0x25) return AKEY_5200_ASTERISK;   /* Shift 8 */
                if (shift && u == 0x20) return AKEY_5200_HASH;       /* Shift 3 */
                return PAD[u - 0x1E];
            }
            if (u >= 0x59 && u <= 0x62) return PAD[u - 0x59];
            if (u == 0x55) return AKEY_5200_ASTERISK;
            if (u == 0x3C) return AKEY_5200_PAUSE;                  /* F3 */
            if (u == 0x3E) return AKEY_5200_RESET;                  /* F5 */
            continue;
        }
        if (ctrl) {                                      /* the Atari's cursor keys */
            if (u == 0x52) return AKEY_CTRL | AKEY_MINUS;
            if (u == 0x51) return AKEY_CTRL | AKEY_EQUAL;
            if (u == 0x50) return AKEY_CTRL | AKEY_PLUS;
            if (u == 0x4F) return AKEY_CTRL | AKEY_ASTERISK;
        }
        UsbKey k = usb_key(u);
        if (k.plain < 0) continue;
        int code = shift ? k.shifted : k.plain;
        if (ctrl) code = (code & ~AKEY_SHFT) | AKEY_CTRL | (shift ? AKEY_SHFT : 0);
        return code;
    }
    return -1;
}

/* Core 0: everything held on the USB ports (the pad and the keyboard). */
static uint32_t usb_buttons(void) {
    static uint32_t seen = 0;
    if (usb_events != seen) {                            /* (logged here: core 0 has the serial) */
        seen = usb_events;
        for (int i = 0; i < 2; i++)
            if (usb_pads[i].on) Serial.printf("[usb] gamepad %d: %04x:%04x%c", i + 1, usb_pads[i].vid,
                                              usb_pads[i].pid, 10);
        Serial.printf("[usb] gamepads %d, keyboard %s%c", usb_pads[0].on + usb_pads[1].on,
                      usb_kbd_on ? "connected" : "none", 10);
    }
    return usb_pads[0].buttons | usb_kbd_buttons();
}

/* Player 2's pad: what it holds (A8U_*). */
static uint32_t usb_pad2(void) { return usb_pads[1].on ? usb_pads[1].buttons : 0; }

/* A pad's A8U_* as Game Settings' slots (A8_GB_* bits). */
static uint32_t usb_slots(uint32_t b) {
    uint32_t s = 0;
    if (b & A8U_A) s |= 1u << A8_GB_RED;
    if (b & A8U_B) s |= 1u << A8_GB_GREEN;
    if (b & A8U_X) s |= 1u << A8_GB_YELLOW;
    if (b & A8U_Y) s |= 1u << A8_GB_BLUE;
    if (b & A8U_L) s |= 1u << A8_GB_SIDE_L;
    if (b & A8U_R) s |= 1u << A8_GB_SIDE_R;
    return s;
}

/* A virtual pin (or button 2 / 3's GPIO) held by the USB pad / keyboard. */
static bool usb_down(int p) {
    const uint32_t b = usb_buttons();
    if (!b) return false;
    const bool combo = (b & A8U_START) && (b & A8U_SELECT);   /* MENU */
    switch (p) {
    case VP_UP:     return b & A8U_UP;
    case VP_DOWN:   return b & A8U_DOWN;
    case VP_LEFT:   return b & A8U_LEFT;
    case VP_RIGHT:  return b & A8U_RIGHT;
    case VP_OPTION: return b & A8U_OPTION;               /* (the keyboard's F2) */
    case VP_SELECT: return (b & A8U_SELECT) && !combo;
    case VP_GREEN:  return b & A8U_B;                    /* the slots: A is Red (button 1's) */
    case VP_YELLOW: return b & A8U_X;
    case VP_BLUE:   return b & A8U_Y;
    case VP_SIDE_L: return b & A8U_L;
    case VP_SIDE_R: return b & A8U_R;
    default: break;
    }
    if (p == PIN_RED)   return b & A8U_A;
    if (p == PIN_START) return (b & A8U_START) && !combo;
    if (p == PIN_MENU)  return combo || (b & A8U_MENU);
    return false;
}

void a8_input_usb_diag(A8UsbDiag *d) {
    for (int n = 0; n < 2; n++) {
        const UsbPad &p = usb_pads[n];
        d->pad[n] = p.on;
        d->vid[n] = p.vid;
        d->pid[n] = p.pid;
        d->buttons[n] = p.buttons;
        d->raw_len[n] = p.raw_len;
        for (int i = 0; i < 16; i++) d->raw[n][i] = p.raw[i];
    }
    d->keyboard = usb_kbd_on;
    for (int i = 0; i < 8; i++) d->kbd[i] = usb_kbd[i];
    d->key = usb_kbd_akey(false);
}
#endif

/* A real button (GPIO) or a key standing in for it. */
/* (or the USB pad / keyboard: A8_USB_HOST) */
static inline bool low(int p) {
#ifdef A8_USB_HOST
    return p >= 0 && ((p < VPIN_BASE && digitalRead(p) == LOW) || vpin_down(p) || usb_down(p));
#else
    return p >= 0 && ((p < VPIN_BASE && digitalRead(p) == LOW) || vpin_down(p));
#endif
}
#else
static inline bool low(int p) { return p >= 0 && digitalRead(p) == LOW; }
#endif

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
#ifdef A8_USB_HOST
    usb_start();                           /* the USB host ports (core 0) */
#endif
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
#if defined(PICO_FRUITJAM) && defined(A8_HEADLESS)
    pinMode(PIN_NEXT_GAME, INPUT_PULLUP);
#endif
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
#ifdef PICO_FRUITJAM
    serial_poll();
#endif
#ifdef A8_USB_HOST
    a8_input_usb_task();
#endif
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
#ifdef PICO_FRUITJAM
        if (low(VP_OPTION)) in.option = 1;          /* (the serial key, the USB keyboard's F2) */
#endif
        if (m.key >= 0) in.key = m.key;
        in.stick[1] |= m.j2;                  /* joystick 2 (twin-stick games) */
        kb_level = m.keyboard;
        scale_level = m.scale;

        /* Right side button (Duo: the 5-way's press): RETURN. On the Duo the
         * press is fire 1 instead — a press on a stick is expected to fire
         * (it was RETURN, which pauses many games); RETURN is on the keyboard. */
        /* (Both are now the defaults of Game Settings' slots, mapped above.) */
    }

#ifdef A8_USB_HOST
    /* Player 2's USB pad: its D-pad joystick 2 (a 5200 game: controller 2),
     * its slots through the game's map - fire to joystick 2, the rest
     * (keys, the console keys) as player 1's; Start / Select START /
     * SELECT (not MENU). */
    if (const uint32_t p2 = usb_pad2()) {
        const bool is5200 = a8_is_5200();
        uint8_t d2 = 0;
        if (p2 & A8U_UP) d2 |= A8_UP;
        if (p2 & A8U_DOWN) d2 |= A8_DOWN;
        if (p2 & A8U_LEFT) d2 |= A8_LEFT;
        if (p2 & A8U_RIGHT) d2 |= A8_RIGHT;
        in.stick[1] |= d2;
        if (d2 && is5200) in.pot[2] = in.pot[3] = A8_POT_NONE;   /* the core reads the directions */
        const uint32_t slots = usb_slots(p2);
        for (int slot = 0; slot < A8_GB_COUNT; slot++) {
            if (!(slots & (1u << slot))) continue;
            uint8_t a = a8_settings_game_action(slot, is5200);
            if (a == A8_ACT_FIRE || a == A8_ACT_TOP_FIRE) in.trig[1] = 1;
            else if (a == A8_ACT_START) { if (is5200) in.key = AKEY_5200_START; else in.start = 1; }
            else if (a == A8_ACT_SELECT) in.select = 1;
            else if (a == A8_ACT_OPTION) in.option = 1;
            else if (a < 0x40 && in.key < 0) in.key = a;          /* an AKEY_* key */
        }
        if (p2 & A8U_START) { if (is5200) in.key = AKEY_5200_START; else in.start = 1; }
        if (p2 & A8U_SELECT) { if (is5200) in.key = AKEY_5200_PAUSE; else in.select = 1; }
    }
    /* a USB keyboard's key (not while the on-screen one is open); F5: the
     * Atari's RESET, once a press */
    if (!a8_osk_is_open()) {
        int k = usb_kbd_akey(a8_is_5200());
        if (k >= 0) in.key = k;
        static bool f5_was = false;
        bool f5 = usb_kbd_on && usb_kbd_has(0x3E) && !a8_is_5200();
        if (f5 && !f5_was) { a8_request_warmstart(); a8_display_toast("RESET"); }
        f5_was = f5;
    }
#endif
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
        if (dx || dy) { a8_osk_move(dx, dy); pico_audio_beep(); }   /* (a move: a beep) */
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

/* Every menu screen polls a8_input_menu: it feeds the menus' sound, and a
 * new press of a direction (a move) beeps (pico_audio.cpp: only while the
 * menus have the sound - not in a game's menus). */
static uint8_t menu_sound(uint8_t b) {
    static uint8_t prev = 0;
    const uint8_t mv = b & (A8M_UP | A8M_DOWN | A8M_LEFT | A8M_RIGHT);
    if (mv & ~prev) pico_audio_menu_beep();
    prev = mv;
    pico_audio_menu_poll();
    return b;
}

#ifdef PICO_FRUITJAM
/* Three buttons: 1 down, 2 OK, 3 tap up / held (0.6 s) back; and the
 * serial monitor's keys. */
uint8_t a8_input_menu(void) {
    serial_poll();
#ifdef A8_USB_HOST
    a8_input_usb_task();
#endif
    uint8_t b = 0;
    static bool m_down = false;
    static uint32_t m_since = 0, up_until = 0;
    uint32_t now = millis();
    bool m = low(PIN_MENU);
    if (m && !m_down) { m_down = true; m_since = now; }
    if (!m && m_down) {
        m_down = false;
        if (now - m_since < 600) up_until = now + 60;   /* a tap: UP for a moment */
    }
    if (m_down && now - m_since >= 600) b |= A8M_BACK;
    if ((int32_t)(up_until - now) > 0) b |= A8M_UP;
    uint8_t d = port_dirs(0);
    if (d & A8_UP)    b |= A8M_UP;
    if ((d & A8_DOWN) || digitalRead(PIN_RED) == LOW) b |= A8M_DOWN;   /* button 1 (a pad's A: OK) */
    if (d & A8_LEFT)  b |= A8M_LEFT;
    if (d & A8_RIGHT) b |= A8M_RIGHT;
    if (low(PIN_START) || port_fire(0)) b |= A8M_OK;
    if (low(PIN_SELECT)) b |= A8M_ALT;
#ifdef A8_USB_HOST
    /* the pad's A: OK; B and the keyboard's Esc / Backspace: back; X: Game
     * Settings (Green on the other devices); Enter: OK */
    const uint32_t u = usb_buttons();
    const bool k_back = usb_kbd_on && (usb_kbd_has(0x29) || usb_kbd_has(0x2A));
    if (u & A8U_A) b |= A8M_OK;
    if (((u & A8U_B) && !(u & A8U_A)) || k_back) { b &= ~A8M_OK; b |= A8M_BACK; }
    if (u & A8U_X) b |= A8M_GREEN;
    if (usb_kbd_on && (usb_kbd_has(0x28) || usb_kbd_has(0x58))) b |= A8M_OK;
#else
    if (low(PIN_GREEN)) b |= A8M_GREEN;
#endif
    return menu_sound(b);
}
#else
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
    return menu_sound(b);
}

#endif

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

#if defined(PICO_FRUITJAM) && defined(A8_HEADLESS)
/* Button 3 pressed (a new press, 30 ms debounce). */
bool a8_input_next_game(void) {
    static bool was_down = true;           /* held at start: not a press */
    static uint32_t since = 0;
    bool down = low(PIN_NEXT_GAME);
    uint32_t now = millis();
    if (down != was_down) {
        if (!since) since = now;
        if (now - since < 30) return false;
        was_down = down;
        since = 0;
        return down;
    }
    since = 0;
    return false;
}
#endif

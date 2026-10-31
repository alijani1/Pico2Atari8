/* a8_ctltest.cpp - Settings > Controller test: every control live, to see
 * that it works and reaches its full range.
 *
 *   Duo 5200: each 5200 controller - plugged in or not, its stick as a dot
 *             (the 5200 pot value it gives the game), the raw charge-time
 *             counts with the rest and range learnt so far, Fire A / B and
 *             the keys held;
 *   Quad:     the four joystick ports' directions and fire; paddles on
 *             ports 1-2 - each one's pot value (1..228, a bar), the raw
 *             charge-time count with the range learnt so far, its button;
 *             the side buttons;
 *   both:     the Joy-Con (raw, centre, range, pot) and the device buttons.
 *
 * Calibration is learnt as you go: move each stick all the way round (turn
 * each paddle fully right) once and the range fills in. MENU goes back.
 */
#include <Arduino.h>
#include <stdarg.h>
#include "a8_ctltest.h"
#include "a8_display.h"
#include "a8_input.h"
#include "a8_platform.h"
#include "pico_5200ctl.h"
#include "pico_paddles.h"

static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_TITLE = 0xFD20,
                      COL_HINT = TFT_DARKGREY, COL_ON = TFT_GREEN, COL_OFF = 0x4208,
                      COL_DOT = 0x07FF, COL_BOX = TFT_DARKGREY;

/* A stick box: the dot at pot (6..220, 114 = centre) x / y. */
struct Box { int x, y, size, dot_x = -1, dot_y = -1; };
static const int DOT = 5;

static void box_frame(Box &b) {
    TFT_eSPI &tft = a8_tft();
    tft.drawRect(b.x, b.y, b.size, b.size, COL_BOX);
    tft.drawFastHLine(b.x + 1, b.y + b.size / 2, b.size - 2, COL_OFF);
    tft.drawFastVLine(b.x + b.size / 2, b.y + 1, b.size - 2, COL_OFF);
    b.dot_x = b.dot_y = -1;
}

static void box_dot(Box &b, int px, int py, bool show) {
    TFT_eSPI &tft = a8_tft();
    int inner = b.size - 2 - DOT;
    int x = show ? b.x + 1 + (px - 6) * inner / 214 : -1;
    int y = show ? b.y + 1 + (py - 6) * inner / 214 : -1;
    if (x == b.dot_x && y == b.dot_y) return;
    if (b.dot_x >= 0) {
        tft.fillRect(b.dot_x, b.dot_y, DOT, DOT, COL_BG);
        /* the cross hairs it may have covered */
        tft.drawFastHLine(b.x + 1, b.y + b.size / 2, b.size - 2, COL_OFF);
        tft.drawFastVLine(b.x + b.size / 2, b.y + 1, b.size - 2, COL_OFF);
    }
    if (show) tft.fillRect(x, y, DOT, DOT, COL_DOT);
    b.dot_x = x;
    b.dot_y = y;
}

static void text_at(int x, int y, uint16_t fg, const char *fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    TFT_eSPI &tft = a8_tft();
    tft.setTextFont(1);
    tft.setTextColor(fg, COL_BG);
    tft.setCursor(x, y);
    tft.print(buf);
}

/* A label lit while its button is held. */
static void lamp(int x, int y, const char *label, bool on) {
    TFT_eSPI &tft = a8_tft();
    int w = (int)strlen(label) * 6 + 6;
    tft.fillRect(x, y, w, 12, on ? COL_ON : COL_OFF);
    tft.setTextFont(1);
    tft.setTextColor(on ? TFT_BLACK : COL_TEXT, on ? COL_ON : COL_OFF);
    tft.setCursor(x + 3, y + 2);
    tft.print(label);
}

static void draw_buttons(int y) {
    static const char *const NAMES[A8B_MENU + 1] = { "RED", "GREEN", "BLUE", "YELLOW", "START",
                                                     "SELECT", "CLICK", "MENU" };
    int x = 4;
    for (int b = 0; b <= A8B_MENU; b++) {
        lamp(x, y, NAMES[b], a8_input_button(b));
        x += (int)strlen(NAMES[b]) * 6 + 10;
    }
}

static void draw_joycon(Box &box, int y) {
    int raw[2], centre[2], lo[2], hi[2];
    int16_t pot[2];
    a8_input_joycon_diag(raw, centre, lo, hi, pot);
    box_dot(box, pot[0] < 0 ? 114 : pot[0], pot[1] < 0 ? 114 : pot[1], true);
    for (int a = 0; a < 2; a++)
        text_at(box.x + box.size + 8, y + a * 10, COL_TEXT, "%c %4d  centre %4d  %4d-%4d  pot %3d  ",
                a ? 'Y' : 'X', raw[a], centre[a], lo[a], hi[a], pot[a] < 0 ? 114 : pot[a]);
}

#ifdef PICO_5200
static const char *const KEY_NAMES[15] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                                           "*", "#", "ST", "PA", "RE" };

static void draw_controller(int n, Box &box) {
    int x0 = n * 160 + 4;
    bool there = ctl5200_present(n);
    text_at(x0, 30, there ? COL_ON : COL_HINT, "CONTROLLER %d %s", n + 1, there ? "OK         " : "not plugged");
    box_dot(box, there ? ctl5200_pot(n, 0) : 114, there ? ctl5200_pot(n, 1) : 114, there);
    for (int a = 0; a < 2; a++) {
        uint32_t c, rest, lo, hi;
        ctl5200_diag(n, a, &c, &rest, &lo, &hi);
        int y = 44 + a * 40;
        int tx = box.x + box.size + 4;
        text_at(tx, y,      COL_TEXT, "%c %4lu   ", a ? 'Y' : 'X', (unsigned long)c);
        text_at(tx, y + 10, COL_HINT, "r %4lu   ", (unsigned long)rest);
        text_at(tx, y + 20, COL_HINT, "%4lu-%4lu", (unsigned long)lo, (unsigned long)hi);
        text_at(tx, y + 30, COL_TEXT, "pot %3d ", there ? ctl5200_pot(n, a) : 114);
    }
    lamp(x0, 128, "FIRE A", ctl5200_fire_a(n));
    lamp(x0 + 50, 128, "FIRE B", ctl5200_fire_b(n));
    char keys[48] = "";
    uint16_t k = ctl5200_keys(n);
    for (int i = 0; i < 15; i++)
        if (k & (1u << i)) { strcat(keys, KEY_NAMES[i]); strcat(keys, " "); }
    text_at(x0, 146, COL_TEXT, "keys %-20s", ctl5200_keypad_found() ? keys : "(no keypad chip)");
}
#endif

#ifdef PICO_QUAD
/* A paddle: its pot as a bar (moving the way the knob turns: 228 at the
 * left, 1 at the right), the count and range, and its button (A = the
 * port's left line, B = right). */
static const int BAR_W = 100;
static void draw_paddles(int n, int bar_at[2]) {
    int x0 = n * 160 + 4;
    bool there = paddles_present(n);
    text_at(x0, 82, there ? COL_ON : COL_HINT, "PADDLES %d %s", n + 1, there ? "OK         " : "not plugged");
    bool fire;
    uint8_t d = a8_input_port_dirs(n, &fire);
    TFT_eSPI &tft = a8_tft();
    for (int a = 0; a < 2; a++) {
        int y = 96 + a * 30;
        int16_t pot = paddles_pot(n, a);
        int bx = pot > 0 ? x0 + 1 + (228 - pot) * (BAR_W - 5) / 227 : -1;
        if (bx != bar_at[a]) {
            tft.fillRect(x0, y, BAR_W, 8, COL_OFF);
            if (bx >= 0) tft.fillRect(bx, y, 3, 8, COL_DOT);
            bar_at[a] = bx;
        }
        lamp(x0 + BAR_W + 6, y - 2, a ? "B" : "A", d & (a ? A8_RIGHT : A8_LEFT));
        uint32_t c, lo, hi;
        paddles_diag(n, a, &c, &lo, &hi);
        text_at(x0, y + 11, COL_TEXT, "%5lu %4lu-%4lu p%3d ", (unsigned long)c,
                (unsigned long)lo, (unsigned long)hi, pot > 0 ? pot : 0);
    }
}
#endif

void a8_ctltest_screen(void) {
    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(2);
    tft.setTextColor(COL_TITLE, COL_BG);
    tft.setCursor(8, 4);
    tft.print("CONTROLLER TEST");
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(220, 4);
    tft.print("MENU: back");
    tft.drawFastHLine(0, 23, 320, COL_HINT);
    text_at(4, 230, COL_HINT, "Move each stick all the way round once to learn its range.");

#ifdef PICO_5200
    Box pads[2] = { { 4, 42, 82 }, { 164, 42, 82 } };
    for (Box &b : pads) box_frame(b);
#endif
#ifdef PICO_QUAD
    int bar_at[2][2] = { { -2, -2 }, { -2, -2 } };       /* force the first draw */
#endif
    Box joy = { 4, 170, 34 };
    text_at(4, 160, COL_HINT, "JOY-CON");
    box_frame(joy);

    while (a8_input_button(A8B_MENU)) delay(10);       /* a press still held: not ours */
    for (;;) {
#ifdef PICO_5200
        a8_input_menu();                                 /* polls the controllers */
        for (int n = 0; n < 2; n++) draw_controller(n, pads[n]);
#else
        for (int n = 0; n < a8_input_num_ports(); n++) {
            bool fire;
            uint8_t d = a8_input_port_dirs(n, &fire);
            int x = 4 + n * 78, y = 34;
            text_at(x, y, COL_TEXT, "PORT %d", n + 1);
            lamp(x, y + 12, "U", d & A8_UP);
            lamp(x + 14, y + 12, "D", d & A8_DOWN);
            lamp(x + 28, y + 12, "L", d & A8_LEFT);
            lamp(x + 42, y + 12, "R", d & A8_RIGHT);
            lamp(x, y + 28, "FIRE", fire);
        }
#ifdef PICO_QUAD
        for (int n = 0; n < 2; n++) draw_paddles(n, bar_at[n]);
        lamp(46, 198, "SIDE L", a8_input_button(A8B_SIDE_L));
        lamp(96, 198, "SIDE R", a8_input_button(A8B_SIDE_R));
#endif
#endif
        draw_joycon(joy, 178);
        draw_buttons(212);
        if (a8_input_button(A8B_MENU)) break;
        delay(16);
    }
    while (a8_input_button(A8B_MENU)) delay(10);
}

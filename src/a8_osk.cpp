/* a8_osk.cpp - the on-screen keyboard: most of the Atari's keys.
 *
 *    1   2   3   4   5   6   7   8   9   0
 *    Q   W   E   R   T   Y   U   I   O   P
 *    A   S   D   F   G   H   J   K   L   ;
 *    Z   X   C   V   B   N   M   ,   .   /
 *   ESC DEL [  SPACE   ] [ RET ]  -   =  DRV
 *
 * DRV isn't an Atari key: it switches disk drive 1 off and on (some disk
 * games won't start while a drive answers).
 *
 * On a 5200 it is the controller's keypad instead:
 *
 *    1   2   3   START
 *    4   5   6   PAUSE
 *    7   8   9   RESET
 *    *   0   #
 *
 * Opened by holding Blue (a8_input.cpp), drawn over the bottom of the
 * picture by a8_display.cpp. The cursor moves on a 10-column grid; up/down
 * land on whichever key covers the same column.
 */
#include <stddef.h>
#include "a8_osk.h"
#include "a8_platform.h"

extern "C" {
#include "akey.h"
}

#define K(l, c) { l, c, 1 }
static const A8Key ROW1[] = { K("1", AKEY_1), K("2", AKEY_2), K("3", AKEY_3), K("4", AKEY_4), K("5", AKEY_5),
                              K("6", AKEY_6), K("7", AKEY_7), K("8", AKEY_8), K("9", AKEY_9), K("0", AKEY_0) };
static const A8Key ROW2[] = { K("Q", AKEY_q), K("W", AKEY_w), K("E", AKEY_e), K("R", AKEY_r), K("T", AKEY_t),
                              K("Y", AKEY_y), K("U", AKEY_u), K("I", AKEY_i), K("O", AKEY_o), K("P", AKEY_p) };
static const A8Key ROW3[] = { K("A", AKEY_a), K("S", AKEY_s), K("D", AKEY_d), K("F", AKEY_f), K("G", AKEY_g),
                              K("H", AKEY_h), K("J", AKEY_j), K("K", AKEY_k), K("L", AKEY_l), K(";", AKEY_SEMICOLON) };
static const A8Key ROW4[] = { K("Z", AKEY_z), K("X", AKEY_x), K("C", AKEY_c), K("V", AKEY_v), K("B", AKEY_b),
                              K("N", AKEY_n), K("M", AKEY_m), K(",", AKEY_COMMA), K(".", AKEY_FULLSTOP), K("/", AKEY_SLASH) };
static const A8Key ROW5[] = { K("ESC", AKEY_ESCAPE), K("DEL", AKEY_BACKSPACE), { "SPACE", AKEY_SPACE, 3 },
                              { "RET", AKEY_RETURN, 2 }, K("-", AKEY_MINUS), K("=", AKEY_EQUAL),
                              K("DRV", A8_OSK_DRIVE) };
#define P(l, c) { l, c, 2 }
static const A8Key PAD1[] = { P("1", AKEY_5200_1), P("2", AKEY_5200_2), P("3", AKEY_5200_3), { "START", AKEY_5200_START, 4 } };
static const A8Key PAD2[] = { P("4", AKEY_5200_4), P("5", AKEY_5200_5), P("6", AKEY_5200_6), { "PAUSE", AKEY_5200_PAUSE, 4 } };
static const A8Key PAD3[] = { P("7", AKEY_5200_7), P("8", AKEY_5200_8), P("9", AKEY_5200_9), { "RESET", AKEY_5200_RESET, 4 } };
static const A8Key PAD4[] = { P("*", AKEY_5200_ASTERISK), P("0", AKEY_5200_0), P("#", AKEY_5200_HASH) };
#undef P
#undef K

struct Row { const A8Key *keys; int n; };
static const Row KEYBOARD[A8_OSK_ROWS] = {
    { ROW1, 10 }, { ROW2, 10 }, { ROW3, 10 }, { ROW4, 10 }, { ROW5, 7 },
};
static const Row KEYPAD[4] = { { PAD1, 4 }, { PAD2, 4 }, { PAD3, 4 }, { PAD4, 3 } };

static volatile bool open_ = false;
static const Row *ROWS = KEYBOARD;
static int n_rows = A8_OSK_ROWS;
static int cur_row = 0, cur_idx = 0;

bool a8_osk_is_open(void) { return open_; }
void a8_osk_open(void) {
    const Row *want = a8_is_5200() ? KEYPAD : KEYBOARD;
    if (want != ROWS) {                       /* the other layout: start at its first key */
        ROWS = want;
        n_rows = want == KEYPAD ? 4 : A8_OSK_ROWS;
        cur_row = cur_idx = 0;
    }
    open_ = true;
}
void a8_osk_close(void) { open_ = false; }

int a8_osk_rows(void) { return n_rows; }

const A8Key *a8_osk_row(int r, int *count) {
    *count = ROWS[r].n;
    return ROWS[r].keys;
}

void a8_osk_cursor(int *row, int *idx) { *row = cur_row; *idx = cur_idx; }

/* First grid column of key idx in row r. */
static int key_col(int r, int idx) {
    int col = 0;
    for (int i = 0; i < idx; i++) col += ROWS[r].keys[i].span;
    return col;
}

/* The key in row r covering grid column col. */
static int key_at(int r, int col) {
    int c = 0;
    for (int i = 0; i < ROWS[r].n; i++) {
        c += ROWS[r].keys[i].span;
        if (col < c) return i;
    }
    return ROWS[r].n - 1;
}

void a8_osk_move(int dx, int dy) {
    if (dx) {
        int n = ROWS[cur_row].n;
        cur_idx = (cur_idx + dx + n) % n;
    }
    if (dy) {
        /* keep the middle of the current key's columns */
        const A8Key &k = ROWS[cur_row].keys[cur_idx];
        int mid = key_col(cur_row, cur_idx) + (k.span - 1) / 2;
        cur_row = (cur_row + dy + n_rows) % n_rows;
        cur_idx = key_at(cur_row, mid);
    }
}

int a8_osk_key(void) { return ROWS[cur_row].keys[cur_idx].code; }

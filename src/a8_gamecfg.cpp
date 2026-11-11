/* a8_gamecfg.cpp - Game Settings: one game's own settings.
 *
 *   Red / Green / Yellow     what each button does in this game: left /
 *   Thumbstick click         right steps through the common choices, FIRE
 *     (Duo: 5-way press)     picks any key from a keyboard (the 5200's
 *   Left / Right side button keypad for a 5200 game)
 *     (Quad)
 *   Thumbstick               Joystick / Paddle (400/800/XL games, a Joy-Con;
 *                            Yellow + click in a game switches it too)
 *   Machine                  Default / 800XL / 400/800 (400/800/XL games;
 *                            from the game's next start)
 *   Disk drive 1             On / Off after load (disk games; was the
 *                            keyboard's DRV: off 3 s after the game stops
 *                            reading the disk, a8_drive1_auto_off_poll)
 *   Reset to defaults
 *
 * Blue, START, SELECT, MENU and the sticks stay as they are. Opened from
 * the game list (Green on a game) or in a game (Blue, then the gear key);
 * kept per game by a8_settings.cpp. A button on its default shows its
 * value in cyan, a changed one in orange.
 */
#include <Arduino.h>
#include <string.h>
#include "a8_gamecfg.h"
#include "a8_settings.h"
#include "a8_display.h"
#include "a8_input.h"
#include "a8_osk.h"
#include "a8_platform.h"

extern "C" {
#include "akey.h"
}

/* The common choices, in the order left / right steps through them. */
static const uint8_t CHOICES_800[] = { A8_ACT_FIRE, A8_ACT_OPTION, A8_ACT_SELECT, A8_ACT_START,
                                       AKEY_SPACE, AKEY_RETURN, AKEY_ESCAPE, A8_ACT_NONE };
static const uint8_t CHOICES_5200[] = { A8_ACT_FIRE, A8_ACT_TOP_FIRE, AKEY_5200_ASTERISK, AKEY_5200_HASH,
                                        AKEY_5200_START, AKEY_5200_PAUSE, AKEY_5200_RESET, A8_ACT_NONE };

#ifdef PICO_DUO
static const char *const SLOT_NAMES[A8_GB_COUNT] = { "Red", "Green", "Yellow", "5-way press",
                                                     "Left side", "Right side" };
#else
static const char *const SLOT_NAMES[A8_GB_COUNT] = { "Red", "Green", "Yellow", "Thumbstick click",
                                                     "Left side button", "Right side button" };
#endif

/* The rows: a button slot (0 .. A8_GB_COUNT - 1), or one of these. */
enum { ROW_STICK = 20, ROW_MACHINE, ROW_DRIVE, ROW_RESET, ROW_BACK };

#ifdef PICO_TINY_SCREEN
static const int ROW_H = 10, LIST_Y = 10, SHOWN = 7, PANEL_W = 160, PANEL_H = 80, FONT = 1, TEXT_X = 2;
#else
/* 11 rows at most (the Quad: 6 buttons, Thumbstick, Machine, Disk drive 1,
 * Reset, Back) between the game's name and the footer */
static const int ROW_H = 16, LIST_Y = 46, SHOWN = 11, PANEL_W = 320, PANEL_H = 240, FONT = 2, TEXT_X = 16;
#endif
static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_SEL_BG = 0x2A69,
                      COL_TITLE = 0xFD20, COL_HINT = TFT_DARKGREY, COL_VALUE = 0x07FF,
                      COL_CHANGED = 0xFD20, COL_KEY = 0x39E7;

static int rows[A8_GB_COUNT + 5], n_rows = 0, first_shown = 0;
static bool g5200 = false, g_in_game = false;
static const char *g_name = "";

/* ---- names --------------------------------------------------------------- */

/* A key's label on the keyboard / keypad, or nullptr. */
static const char *key_label(int code, bool is5200) {
    for (int r = 0; r < a8_osk_layout_rows(is5200); r++) {
        int n;
        const A8Key *k = a8_osk_layout_row(is5200, r, &n);
        for (int i = 0; i < n; i++) if (k[i].code == code) return k[i].label;
    }
    return nullptr;
}

static void action_name(uint8_t a, char *buf, size_t cap) {
    switch (a) {
    case A8_ACT_NONE:     snprintf(buf, cap, "None"); return;
    case A8_ACT_FIRE:     snprintf(buf, cap, "%s", g5200 ? "Bottom fire" : "Fire"); return;
    case A8_ACT_TOP_FIRE: snprintf(buf, cap, "Top fire"); return;
    case A8_ACT_START:    snprintf(buf, cap, "START"); return;
    case A8_ACT_SELECT:   snprintf(buf, cap, "SELECT"); return;
    case A8_ACT_OPTION:   snprintf(buf, cap, "OPTION"); return;
    }
    if (!g5200 && a == AKEY_RETURN) { snprintf(buf, cap, "RETURN"); return; }
    const char *l = key_label(a, g5200);
    if (!l) { snprintf(buf, cap, "Key %02X", a); return; }
    if (strlen(l) > 1) snprintf(buf, cap, "%s", l);          /* SPACE, ESC, START ... */
    else snprintf(buf, cap, "Key %s", l);
}

/* ---- drawing ------------------------------------------------------------- */

static void value_text(int row, char *buf, size_t cap, bool *changed) {
    *changed = false;
    if (row < A8_GB_COUNT) {
        uint8_t a = a8_settings_game_action(row, g5200);
        *changed = a != a8_settings_default_action(row, g5200);
        action_name(a, buf, cap);
        return;
    }
    switch (row) {
    case ROW_STICK:
        snprintf(buf, cap, "%s", a8_settings_game_paddle() ? "Paddle" : "Joystick");
        *changed = a8_settings_game_paddle();
        return;
    case ROW_MACHINE: {
        static const char *const M[A8_MACH_COUNT] = { "Default", "800XL", "400/800" };
        int m = a8_settings_game_machine();
        snprintf(buf, cap, "%s", M[m]);
        *changed = m != A8_MACH_DEFAULT;
        return;
    }
    case ROW_DRIVE:
#ifdef PICO_TINY_SCREEN
        snprintf(buf, cap, "%s", a8_settings_game_drive_off() ? "Off at load" : "On");
#else
        snprintf(buf, cap, "%s", a8_settings_game_drive_off() ? "Off after load" : "On");
#endif
        *changed = a8_settings_game_drive_off();
        return;
    default: buf[0] = 0; return;
    }
}

static const char *row_name(int row) {
    if (row < A8_GB_COUNT) return SLOT_NAMES[row];
    switch (row) {
    case ROW_STICK: return "Thumbstick";
    case ROW_MACHINE: return "Machine";
    case ROW_DRIVE: return "Disk drive 1";
    case ROW_RESET: return "Reset to defaults";
    default:        return "Back";
    }
}

static void draw_row(int i, bool sel) {
    if (i < first_shown || i >= first_shown + SHOWN) return;
    TFT_eSPI &tft = a8_tft();
    const int row = rows[i];
    int y = LIST_Y + (i - first_shown) * ROW_H;
    uint16_t bg = sel ? COL_SEL_BG : COL_BG;
    tft.fillRect(0, y, PANEL_W, ROW_H, bg);
    tft.setTextFont(FONT);
    tft.setTextColor(COL_TEXT, bg);
    tft.setCursor(TEXT_X, y + 1);
    tft.print(row_name(row));
    char v[24];
    bool changed;
    value_text(row, v, sizeof(v), &changed);
    if (v[0]) {
        tft.setTextColor(sel ? COL_TEXT : changed ? COL_CHANGED : COL_VALUE, bg);
        tft.setCursor(PANEL_W - TEXT_X - tft.textWidth(v), y + 1);
        tft.print(v);
    }
}

static void draw_screen(int sel) {
    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(FONT);
    tft.setTextWrap(false);
    tft.setTextColor(COL_TITLE, COL_BG);
#ifdef PICO_TINY_SCREEN
    tft.setCursor(TEXT_X, 0);
    tft.print("GAME SETTINGS");
    tft.drawFastHLine(0, 8, PANEL_W, COL_HINT);
    for (int i = 0; i < n_rows; i++) draw_row(i, i == sel);
    if (first_shown > 0) tft.fillTriangle(156, LIST_Y + 1, 153, LIST_Y + 4, 159, LIST_Y + 4, COL_HINT);
    if (first_shown + SHOWN < n_rows) {
        int yb = LIST_Y + SHOWN * ROW_H - 2;
        tft.fillTriangle(156, yb, 153, yb - 3, 159, yb - 3, COL_HINT);
    }
#else
    tft.setCursor(8, 6);
    tft.print("GAME SETTINGS");
    tft.drawFastHLine(0, 25, PANEL_W, COL_HINT);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 28);
    tft.print(g_name);                      /* (clipped at the panel's edge) */
    for (int i = 0; i < n_rows; i++) draw_row(i, i == sel);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print("LEFT/RIGHT: change  FIRE: any key  MENU: back");   /* 303 px: fits */
#endif
}

/* ---- input --------------------------------------------------------------- */

/* New presses, with auto-repeat for held directions. */
struct Repeat { uint8_t prev = 0xFF; uint32_t held_since = 0, last = 0; };
static uint8_t presses(Repeat &r) {
    uint8_t b = a8_input_menu();
    uint8_t pressed = b & ~r.prev;
    uint32_t now = millis();
    uint8_t dirs = b & (A8M_UP | A8M_DOWN | A8M_LEFT | A8M_RIGHT);
    if (dirs && dirs == (r.prev & dirs)) {
        if (!r.held_since) r.held_since = now;
        if (now - r.held_since > 400 && now - r.last > 120) { pressed |= dirs; r.last = now; }
    } else {
        r.held_since = 0;
    }
    r.prev = b;
    return pressed;
}

static void wait_release(void) {
    for (uint32_t t0 = millis(); a8_input_menu() && millis() - t0 < 1000; ) delay(10);
}

/* ---- the key picker ------------------------------------------------------ */

/* The keyboard (a 5200 game: the keypad) without the gear / DRV keys. */
static const int MAXR = A8_OSK_ROWS, MAXK = A8_OSK_COLS;
static A8Key pk[MAXR][MAXK];
static int pk_n[MAXR], pk_rows = 0;

static int pk_col(int r, int idx) {
    int c = 0;
    for (int i = 0; i < idx; i++) c += pk[r][i].span;
    return c;
}
static int pk_at(int r, int col) {
    int c = 0;
    for (int i = 0; i < pk_n[r]; i++) { c += pk[r][i].span; if (col < c) return i; }
    return pk_n[r] - 1;
}

#ifdef PICO_TINY_SCREEN
static const int PK_W = PANEL_W / A8_OSK_COLS, PK_H = 11, PK_Y = 12;
#else
static const int PK_W = PANEL_W / A8_OSK_COLS, PK_H = 24, PK_Y = 70;
#endif

static void pk_draw_key(int r, int i, bool sel) {
    TFT_eSPI &tft = a8_tft();
    int x = pk_col(r, i) * PK_W + 1, w = pk[r][i].span * PK_W - 2;
    int y = PK_Y + r * PK_H + 1, h = PK_H - 2;
    uint16_t face = sel ? COL_SEL_BG : COL_KEY;
    tft.fillRect(x, y, w, h, face);
    tft.setTextFont(FONT);
    tft.setTextColor(COL_TEXT, face);
    const char *t = pk[r][i].label;
    tft.setCursor(x + (w - tft.textWidth(t)) / 2, y + (h - (FONT == 1 ? 8 : 16)) / 2 + 1);
    tft.print(t);
}

/* Any key for the button `slot`: its AKEY_* code, or -1 (MENU: none). */
static int pick_key(int slot, uint8_t current) {
    pk_rows = 0;
    for (int r = 0; r < a8_osk_layout_rows(g5200); r++) {
        int n;
        const A8Key *k = a8_osk_layout_row(g5200, r, &n);
        pk_n[pk_rows] = 0;
        for (int i = 0; i < n && pk_n[pk_rows] < MAXK; i++)
            if (k[i].code >= 0) pk[pk_rows][pk_n[pk_rows]++] = k[i];
        if (pk_n[pk_rows]) pk_rows++;
    }
    int cr = 0, ci = 0;                      /* start on the button's key, if it has one */
    for (int r = 0; r < pk_rows; r++)
        for (int i = 0; i < pk_n[r]; i++)
            if (pk[r][i].code == current) { cr = r; ci = i; }

    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(FONT);
    tft.setTextColor(COL_TITLE, COL_BG);
    char title[40];
    snprintf(title, sizeof(title), "%s: PICK A KEY", SLOT_NAMES[slot]);
#ifdef PICO_TINY_SCREEN
    tft.setCursor(TEXT_X, 0);
    tft.print(title);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(TEXT_X, 70);
    tft.print("Fire: pick  Menu: back");
#else
    tft.setCursor(8, 6);
    tft.print(title);
    tft.drawFastHLine(0, 25, PANEL_W, COL_HINT);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print("FIRE: pick   MENU: back");
#endif
    for (int r = 0; r < pk_rows; r++)
        for (int i = 0; i < pk_n[r]; i++) pk_draw_key(r, i, r == cr && i == ci);

    wait_release();
    Repeat rep;
    for (;;) {
        uint8_t p = presses(rep);
        int or_ = cr, oi = ci;
        if (p & (A8M_LEFT | A8M_RIGHT)) {
            int n = pk_n[cr];
            ci = (ci + ((p & A8M_RIGHT) ? 1 : -1) + n) % n;
        }
        if (p & (A8M_UP | A8M_DOWN)) {
            int mid = pk_col(cr, ci) + (pk[cr][ci].span - 1) / 2;
            cr = (cr + ((p & A8M_DOWN) ? 1 : -1) + pk_rows) % pk_rows;
            ci = pk_at(cr, mid);
        }
        if (cr != or_ || ci != oi) { pk_draw_key(or_, oi, false); pk_draw_key(cr, ci, true); }
        if (p & A8M_OK) { wait_release(); return pk[cr][ci].code; }
        if (p & A8M_BACK) { wait_release(); return -1; }
        delay(10);
    }
}

/* ---- the screen ---------------------------------------------------------- */

/* left / right on a button row: the next / previous common choice */
static void step_choice(int slot, int dir) {
    const uint8_t *c = g5200 ? CHOICES_5200 : CHOICES_800;
    const int n = g5200 ? (int)sizeof(CHOICES_5200) : (int)sizeof(CHOICES_800);
    uint8_t now = a8_settings_game_action(slot, g5200);
    int at = -1;
    for (int i = 0; i < n; i++) if (c[i] == now) at = i;
    int next = at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n;   /* a picked key: to an end */
    a8_settings_set_game_action(slot, g5200, c[next]);
}

void a8_gamecfg_screen(const char *name, bool is5200, bool in_game, bool has_disk) {
    g5200 = is5200;
    g_in_game = in_game;
    g_name = name ? name : "";
    n_rows = 0;
    for (int s = 0; s < A8_GB_COUNT; s++) if (a8_input_has_slot(s)) rows[n_rows++] = s;
    if (!is5200 && a8_input_has_joycon()) rows[n_rows++] = ROW_STICK;
    if (!is5200) rows[n_rows++] = ROW_MACHINE;
    if (!is5200 && has_disk) rows[n_rows++] = ROW_DRIVE;
    rows[n_rows++] = ROW_RESET;
    rows[n_rows++] = ROW_BACK;

    int sel = 0;
    first_shown = 0;
    draw_screen(sel);
    wait_release();
    Repeat rep;
    for (;;) {
        uint8_t p = presses(rep);
        int old = sel;
        if (p & A8M_UP)   sel = sel > 0 ? sel - 1 : n_rows - 1;
        if (p & A8M_DOWN) sel = sel < n_rows - 1 ? sel + 1 : 0;
        if (sel != old) {
            int top = first_shown;
            if (sel < first_shown) first_shown = sel;
            if (sel >= first_shown + SHOWN) first_shown = sel - SHOWN + 1;
            if (first_shown != top) draw_screen(sel);
            else { draw_row(old, false); draw_row(sel, true); }
        }
        const int row = rows[sel];
        int dir = (p & A8M_LEFT) ? -1 : (p & A8M_RIGHT) ? 1 : 0;
        if (row < A8_GB_COUNT) {
            if (dir) { step_choice(row, dir); draw_row(sel, true); }
            if (p & A8M_OK) {
                int k = pick_key(row, a8_settings_game_action(row, g5200));
                if (k >= 0) a8_settings_set_game_action(row, g5200, (uint8_t)k);
                draw_screen(sel);
                rep = Repeat();
                continue;
            }
        } else if (row == ROW_STICK && (dir || (p & A8M_OK))) {
            a8_settings_set_game_paddle(!a8_settings_game_paddle());
            draw_row(sel, true);
        } else if (row == ROW_MACHINE && (dir || (p & A8M_OK))) {
            int m = a8_settings_game_machine() + (dir < 0 ? -1 : 1);
            a8_settings_set_game_machine((m + A8_MACH_COUNT) % A8_MACH_COUNT);
            draw_row(sel, true);
        } else if (row == ROW_DRIVE && (dir || (p & A8M_OK))) {
            bool off = !a8_settings_game_drive_off();
            a8_settings_set_game_drive_off(off);
            /* in a game: now, too (it has loaded, or the user wouldn't ask) */
            if (g_in_game && off == (bool)a8_drive1_is_on()) a8_toggle_drive1();
            draw_row(sel, true);
        } else if (row == ROW_RESET && (p & A8M_OK)) {
            a8_settings_game_reset();
            draw_screen(sel);
        } else if (row == ROW_BACK && (p & A8M_OK)) {
            break;
        }
        if (p & A8M_BACK) break;
        delay(10);
    }
    while (a8_input_menu() & (A8M_BACK | A8M_OK)) delay(10);
}
